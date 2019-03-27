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
 * cvmx-dfa-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon dfa.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_DFA_DEFS_H__
#define __CVMX_DFA_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_BIST0 CVMX_DFA_BIST0_FUNC()
static inline uint64_t CVMX_DFA_BIST0_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_DFA_BIST0 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800370007F0ull);
}
#else
#define CVMX_DFA_BIST0 (CVMX_ADD_IO_SEG(0x00011800370007F0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_BIST1 CVMX_DFA_BIST1_FUNC()
static inline uint64_t CVMX_DFA_BIST1_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_DFA_BIST1 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800370007F8ull);
}
#else
#define CVMX_DFA_BIST1 (CVMX_ADD_IO_SEG(0x00011800370007F8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_BST0 CVMX_DFA_BST0_FUNC()
static inline uint64_t CVMX_DFA_BST0_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN31XX) || OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_DFA_BST0 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800300007F0ull);
}
#else
#define CVMX_DFA_BST0 (CVMX_ADD_IO_SEG(0x00011800300007F0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_BST1 CVMX_DFA_BST1_FUNC()
static inline uint64_t CVMX_DFA_BST1_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN31XX) || OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_DFA_BST1 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800300007F8ull);
}
#else
#define CVMX_DFA_BST1 (CVMX_ADD_IO_SEG(0x00011800300007F8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_CFG CVMX_DFA_CFG_FUNC()
static inline uint64_t CVMX_DFA_CFG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_DFA_CFG not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180030000000ull);
}
#else
#define CVMX_DFA_CFG (CVMX_ADD_IO_SEG(0x0001180030000000ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_CONFIG CVMX_DFA_CONFIG_FUNC()
static inline uint64_t CVMX_DFA_CONFIG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_DFA_CONFIG not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180037000000ull);
}
#else
#define CVMX_DFA_CONFIG (CVMX_ADD_IO_SEG(0x0001180037000000ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_CONTROL CVMX_DFA_CONTROL_FUNC()
static inline uint64_t CVMX_DFA_CONTROL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_DFA_CONTROL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180037000020ull);
}
#else
#define CVMX_DFA_CONTROL (CVMX_ADD_IO_SEG(0x0001180037000020ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_DBELL CVMX_DFA_DBELL_FUNC()
static inline uint64_t CVMX_DFA_DBELL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN31XX) || OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN58XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_DFA_DBELL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001370000000000ull);
}
#else
#define CVMX_DFA_DBELL (CVMX_ADD_IO_SEG(0x0001370000000000ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_DDR2_ADDR CVMX_DFA_DDR2_ADDR_FUNC()
static inline uint64_t CVMX_DFA_DDR2_ADDR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN31XX)))
		cvmx_warn("CVMX_DFA_DDR2_ADDR not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180030000210ull);
}
#else
#define CVMX_DFA_DDR2_ADDR (CVMX_ADD_IO_SEG(0x0001180030000210ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_DDR2_BUS CVMX_DFA_DDR2_BUS_FUNC()
static inline uint64_t CVMX_DFA_DDR2_BUS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN31XX)))
		cvmx_warn("CVMX_DFA_DDR2_BUS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180030000080ull);
}
#else
#define CVMX_DFA_DDR2_BUS (CVMX_ADD_IO_SEG(0x0001180030000080ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_DDR2_CFG CVMX_DFA_DDR2_CFG_FUNC()
static inline uint64_t CVMX_DFA_DDR2_CFG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN31XX)))
		cvmx_warn("CVMX_DFA_DDR2_CFG not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180030000208ull);
}
#else
#define CVMX_DFA_DDR2_CFG (CVMX_ADD_IO_SEG(0x0001180030000208ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_DDR2_COMP CVMX_DFA_DDR2_COMP_FUNC()
static inline uint64_t CVMX_DFA_DDR2_COMP_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN31XX)))
		cvmx_warn("CVMX_DFA_DDR2_COMP not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180030000090ull);
}
#else
#define CVMX_DFA_DDR2_COMP (CVMX_ADD_IO_SEG(0x0001180030000090ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_DDR2_EMRS CVMX_DFA_DDR2_EMRS_FUNC()
static inline uint64_t CVMX_DFA_DDR2_EMRS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN31XX)))
		cvmx_warn("CVMX_DFA_DDR2_EMRS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180030000268ull);
}
#else
#define CVMX_DFA_DDR2_EMRS (CVMX_ADD_IO_SEG(0x0001180030000268ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_DDR2_FCNT CVMX_DFA_DDR2_FCNT_FUNC()
static inline uint64_t CVMX_DFA_DDR2_FCNT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN31XX)))
		cvmx_warn("CVMX_DFA_DDR2_FCNT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180030000078ull);
}
#else
#define CVMX_DFA_DDR2_FCNT (CVMX_ADD_IO_SEG(0x0001180030000078ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_DDR2_MRS CVMX_DFA_DDR2_MRS_FUNC()
static inline uint64_t CVMX_DFA_DDR2_MRS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN31XX)))
		cvmx_warn("CVMX_DFA_DDR2_MRS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180030000260ull);
}
#else
#define CVMX_DFA_DDR2_MRS (CVMX_ADD_IO_SEG(0x0001180030000260ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_DDR2_OPT CVMX_DFA_DDR2_OPT_FUNC()
static inline uint64_t CVMX_DFA_DDR2_OPT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN31XX)))
		cvmx_warn("CVMX_DFA_DDR2_OPT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180030000070ull);
}
#else
#define CVMX_DFA_DDR2_OPT (CVMX_ADD_IO_SEG(0x0001180030000070ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_DDR2_PLL CVMX_DFA_DDR2_PLL_FUNC()
static inline uint64_t CVMX_DFA_DDR2_PLL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN31XX)))
		cvmx_warn("CVMX_DFA_DDR2_PLL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180030000088ull);
}
#else
#define CVMX_DFA_DDR2_PLL (CVMX_ADD_IO_SEG(0x0001180030000088ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_DDR2_TMG CVMX_DFA_DDR2_TMG_FUNC()
static inline uint64_t CVMX_DFA_DDR2_TMG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN31XX)))
		cvmx_warn("CVMX_DFA_DDR2_TMG not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180030000218ull);
}
#else
#define CVMX_DFA_DDR2_TMG (CVMX_ADD_IO_SEG(0x0001180030000218ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_DEBUG0 CVMX_DFA_DEBUG0_FUNC()
static inline uint64_t CVMX_DFA_DEBUG0_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_DFA_DEBUG0 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180037000040ull);
}
#else
#define CVMX_DFA_DEBUG0 (CVMX_ADD_IO_SEG(0x0001180037000040ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_DEBUG1 CVMX_DFA_DEBUG1_FUNC()
static inline uint64_t CVMX_DFA_DEBUG1_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_DFA_DEBUG1 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180037000048ull);
}
#else
#define CVMX_DFA_DEBUG1 (CVMX_ADD_IO_SEG(0x0001180037000048ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_DEBUG2 CVMX_DFA_DEBUG2_FUNC()
static inline uint64_t CVMX_DFA_DEBUG2_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_DFA_DEBUG2 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180037000050ull);
}
#else
#define CVMX_DFA_DEBUG2 (CVMX_ADD_IO_SEG(0x0001180037000050ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_DEBUG3 CVMX_DFA_DEBUG3_FUNC()
static inline uint64_t CVMX_DFA_DEBUG3_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_DFA_DEBUG3 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180037000058ull);
}
#else
#define CVMX_DFA_DEBUG3 (CVMX_ADD_IO_SEG(0x0001180037000058ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_DIFCTL CVMX_DFA_DIFCTL_FUNC()
static inline uint64_t CVMX_DFA_DIFCTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN31XX) || OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN58XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_DFA_DIFCTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001370600000000ull);
}
#else
#define CVMX_DFA_DIFCTL (CVMX_ADD_IO_SEG(0x0001370600000000ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_DIFRDPTR CVMX_DFA_DIFRDPTR_FUNC()
static inline uint64_t CVMX_DFA_DIFRDPTR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN31XX) || OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN58XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_DFA_DIFRDPTR not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001370200000000ull);
}
#else
#define CVMX_DFA_DIFRDPTR (CVMX_ADD_IO_SEG(0x0001370200000000ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_DTCFADR CVMX_DFA_DTCFADR_FUNC()
static inline uint64_t CVMX_DFA_DTCFADR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_DFA_DTCFADR not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180037000060ull);
}
#else
#define CVMX_DFA_DTCFADR (CVMX_ADD_IO_SEG(0x0001180037000060ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_ECLKCFG CVMX_DFA_ECLKCFG_FUNC()
static inline uint64_t CVMX_DFA_ECLKCFG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN31XX)))
		cvmx_warn("CVMX_DFA_ECLKCFG not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180030000200ull);
}
#else
#define CVMX_DFA_ECLKCFG (CVMX_ADD_IO_SEG(0x0001180030000200ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_ERR CVMX_DFA_ERR_FUNC()
static inline uint64_t CVMX_DFA_ERR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN31XX) || OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_DFA_ERR not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180030000028ull);
}
#else
#define CVMX_DFA_ERR (CVMX_ADD_IO_SEG(0x0001180030000028ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_ERROR CVMX_DFA_ERROR_FUNC()
static inline uint64_t CVMX_DFA_ERROR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_DFA_ERROR not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180037000028ull);
}
#else
#define CVMX_DFA_ERROR (CVMX_ADD_IO_SEG(0x0001180037000028ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_INTMSK CVMX_DFA_INTMSK_FUNC()
static inline uint64_t CVMX_DFA_INTMSK_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_DFA_INTMSK not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180037000030ull);
}
#else
#define CVMX_DFA_INTMSK (CVMX_ADD_IO_SEG(0x0001180037000030ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_MEMCFG0 CVMX_DFA_MEMCFG0_FUNC()
static inline uint64_t CVMX_DFA_MEMCFG0_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_DFA_MEMCFG0 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180030000008ull);
}
#else
#define CVMX_DFA_MEMCFG0 (CVMX_ADD_IO_SEG(0x0001180030000008ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_MEMCFG1 CVMX_DFA_MEMCFG1_FUNC()
static inline uint64_t CVMX_DFA_MEMCFG1_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_DFA_MEMCFG1 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180030000010ull);
}
#else
#define CVMX_DFA_MEMCFG1 (CVMX_ADD_IO_SEG(0x0001180030000010ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_MEMCFG2 CVMX_DFA_MEMCFG2_FUNC()
static inline uint64_t CVMX_DFA_MEMCFG2_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_DFA_MEMCFG2 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180030000060ull);
}
#else
#define CVMX_DFA_MEMCFG2 (CVMX_ADD_IO_SEG(0x0001180030000060ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_MEMFADR CVMX_DFA_MEMFADR_FUNC()
static inline uint64_t CVMX_DFA_MEMFADR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN31XX) || OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_DFA_MEMFADR not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180030000030ull);
}
#else
#define CVMX_DFA_MEMFADR (CVMX_ADD_IO_SEG(0x0001180030000030ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_MEMFCR CVMX_DFA_MEMFCR_FUNC()
static inline uint64_t CVMX_DFA_MEMFCR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_DFA_MEMFCR not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180030000038ull);
}
#else
#define CVMX_DFA_MEMFCR (CVMX_ADD_IO_SEG(0x0001180030000038ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_MEMHIDAT CVMX_DFA_MEMHIDAT_FUNC()
static inline uint64_t CVMX_DFA_MEMHIDAT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_DFA_MEMHIDAT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001370700000000ull);
}
#else
#define CVMX_DFA_MEMHIDAT (CVMX_ADD_IO_SEG(0x0001370700000000ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_MEMRLD CVMX_DFA_MEMRLD_FUNC()
static inline uint64_t CVMX_DFA_MEMRLD_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_DFA_MEMRLD not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180030000018ull);
}
#else
#define CVMX_DFA_MEMRLD (CVMX_ADD_IO_SEG(0x0001180030000018ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_NCBCTL CVMX_DFA_NCBCTL_FUNC()
static inline uint64_t CVMX_DFA_NCBCTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_DFA_NCBCTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180030000020ull);
}
#else
#define CVMX_DFA_NCBCTL (CVMX_ADD_IO_SEG(0x0001180030000020ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_PFC0_CNT CVMX_DFA_PFC0_CNT_FUNC()
static inline uint64_t CVMX_DFA_PFC0_CNT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_DFA_PFC0_CNT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180037000090ull);
}
#else
#define CVMX_DFA_PFC0_CNT (CVMX_ADD_IO_SEG(0x0001180037000090ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_PFC0_CTL CVMX_DFA_PFC0_CTL_FUNC()
static inline uint64_t CVMX_DFA_PFC0_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_DFA_PFC0_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180037000088ull);
}
#else
#define CVMX_DFA_PFC0_CTL (CVMX_ADD_IO_SEG(0x0001180037000088ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_PFC1_CNT CVMX_DFA_PFC1_CNT_FUNC()
static inline uint64_t CVMX_DFA_PFC1_CNT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_DFA_PFC1_CNT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800370000A0ull);
}
#else
#define CVMX_DFA_PFC1_CNT (CVMX_ADD_IO_SEG(0x00011800370000A0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_PFC1_CTL CVMX_DFA_PFC1_CTL_FUNC()
static inline uint64_t CVMX_DFA_PFC1_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_DFA_PFC1_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180037000098ull);
}
#else
#define CVMX_DFA_PFC1_CTL (CVMX_ADD_IO_SEG(0x0001180037000098ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_PFC2_CNT CVMX_DFA_PFC2_CNT_FUNC()
static inline uint64_t CVMX_DFA_PFC2_CNT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_DFA_PFC2_CNT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800370000B0ull);
}
#else
#define CVMX_DFA_PFC2_CNT (CVMX_ADD_IO_SEG(0x00011800370000B0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_PFC2_CTL CVMX_DFA_PFC2_CTL_FUNC()
static inline uint64_t CVMX_DFA_PFC2_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_DFA_PFC2_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800370000A8ull);
}
#else
#define CVMX_DFA_PFC2_CTL (CVMX_ADD_IO_SEG(0x00011800370000A8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_PFC3_CNT CVMX_DFA_PFC3_CNT_FUNC()
static inline uint64_t CVMX_DFA_PFC3_CNT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_DFA_PFC3_CNT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800370000C0ull);
}
#else
#define CVMX_DFA_PFC3_CNT (CVMX_ADD_IO_SEG(0x00011800370000C0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_PFC3_CTL CVMX_DFA_PFC3_CTL_FUNC()
static inline uint64_t CVMX_DFA_PFC3_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_DFA_PFC3_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800370000B8ull);
}
#else
#define CVMX_DFA_PFC3_CTL (CVMX_ADD_IO_SEG(0x00011800370000B8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_PFC_GCTL CVMX_DFA_PFC_GCTL_FUNC()
static inline uint64_t CVMX_DFA_PFC_GCTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_DFA_PFC_GCTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180037000080ull);
}
#else
#define CVMX_DFA_PFC_GCTL (CVMX_ADD_IO_SEG(0x0001180037000080ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_RODT_COMP_CTL CVMX_DFA_RODT_COMP_CTL_FUNC()
static inline uint64_t CVMX_DFA_RODT_COMP_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_DFA_RODT_COMP_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180030000068ull);
}
#else
#define CVMX_DFA_RODT_COMP_CTL (CVMX_ADD_IO_SEG(0x0001180030000068ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_SBD_DBG0 CVMX_DFA_SBD_DBG0_FUNC()
static inline uint64_t CVMX_DFA_SBD_DBG0_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN31XX) || OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_DFA_SBD_DBG0 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180030000040ull);
}
#else
#define CVMX_DFA_SBD_DBG0 (CVMX_ADD_IO_SEG(0x0001180030000040ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_SBD_DBG1 CVMX_DFA_SBD_DBG1_FUNC()
static inline uint64_t CVMX_DFA_SBD_DBG1_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN31XX) || OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_DFA_SBD_DBG1 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180030000048ull);
}
#else
#define CVMX_DFA_SBD_DBG1 (CVMX_ADD_IO_SEG(0x0001180030000048ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_SBD_DBG2 CVMX_DFA_SBD_DBG2_FUNC()
static inline uint64_t CVMX_DFA_SBD_DBG2_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN31XX) || OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_DFA_SBD_DBG2 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180030000050ull);
}
#else
#define CVMX_DFA_SBD_DBG2 (CVMX_ADD_IO_SEG(0x0001180030000050ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFA_SBD_DBG3 CVMX_DFA_SBD_DBG3_FUNC()
static inline uint64_t CVMX_DFA_SBD_DBG3_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN31XX) || OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_DFA_SBD_DBG3 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180030000058ull);
}
#else
#define CVMX_DFA_SBD_DBG3 (CVMX_ADD_IO_SEG(0x0001180030000058ull))
#endif

/**
 * cvmx_dfa_bist0
 *
 * DFA_BIST0 = DFA Bist Status (per-DTC)
 *
 * Description:
 */
union cvmx_dfa_bist0 {
	uint64_t u64;
	struct cvmx_dfa_bist0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_27_63               : 37;
	uint64_t gfb                          : 3;  /**< Bist Results for GFB RAM(s) (per-cluster)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t reserved_22_23               : 2;
	uint64_t stx2                         : 2;  /**< Bist Results for STX2 RAM(s)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t stx1                         : 2;  /**< Bist Results for STX1 RAM(s)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t stx                          : 2;  /**< Bist Results for STX0 RAM(s)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t reserved_14_15               : 2;
	uint64_t dtx2                         : 2;  /**< Bist Results for DTX2 RAM(s)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t dtx1                         : 2;  /**< Bist Results for DTX1 RAM(s)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t dtx                          : 2;  /**< Bist Results for DTX0 RAM(s)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t reserved_7_7                 : 1;
	uint64_t rdf                          : 3;  /**< Bist Results for RWB RAM(s) (per-cluster)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t reserved_3_3                 : 1;
	uint64_t pdb                          : 3;  /**< Bist Results for PDB RAM(s) (per-cluster)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
#else
	uint64_t pdb                          : 3;
	uint64_t reserved_3_3                 : 1;
	uint64_t rdf                          : 3;
	uint64_t reserved_7_7                 : 1;
	uint64_t dtx                          : 2;
	uint64_t dtx1                         : 2;
	uint64_t dtx2                         : 2;
	uint64_t reserved_14_15               : 2;
	uint64_t stx                          : 2;
	uint64_t stx1                         : 2;
	uint64_t stx2                         : 2;
	uint64_t reserved_22_23               : 2;
	uint64_t gfb                          : 3;
	uint64_t reserved_27_63               : 37;
#endif
	} s;
	struct cvmx_dfa_bist0_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_25_63               : 39;
	uint64_t gfb                          : 1;  /**< Bist Results for GFB RAM(s) (per-cluster)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t reserved_18_23               : 6;
	uint64_t stx                          : 2;  /**< Bist Results for STX0 RAM(s)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t reserved_10_15               : 6;
	uint64_t dtx                          : 2;  /**< Bist Results for DTX0 RAM(s)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t reserved_5_7                 : 3;
	uint64_t rdf                          : 1;  /**< Bist Results for RWB RAM(s) (per-cluster)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t reserved_1_3                 : 3;
	uint64_t pdb                          : 1;  /**< Bist Results for PDB RAM(s) (per-cluster)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
#else
	uint64_t pdb                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t rdf                          : 1;
	uint64_t reserved_5_7                 : 3;
	uint64_t dtx                          : 2;
	uint64_t reserved_10_15               : 6;
	uint64_t stx                          : 2;
	uint64_t reserved_18_23               : 6;
	uint64_t gfb                          : 1;
	uint64_t reserved_25_63               : 39;
#endif
	} cn61xx;
	struct cvmx_dfa_bist0_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_29_63               : 35;
	uint64_t mwb                          : 1;  /**< Bist Results for MWB RAM(s)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t reserved_25_27               : 3;
	uint64_t gfb                          : 1;  /**< Bist Results for GFB RAM(s)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t reserved_18_23               : 6;
	uint64_t stx                          : 2;  /**< Bist Results for STX RAM(s)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t reserved_10_15               : 6;
	uint64_t dtx                          : 2;  /**< Bist Results for DTX RAM(s)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t reserved_5_7                 : 3;
	uint64_t rdf                          : 1;  /**< Bist Results for RWB[3:0] RAM(s)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t reserved_1_3                 : 3;
	uint64_t pdb                          : 1;  /**< Bist Results for PDB RAM(s)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
#else
	uint64_t pdb                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t rdf                          : 1;
	uint64_t reserved_5_7                 : 3;
	uint64_t dtx                          : 2;
	uint64_t reserved_10_15               : 6;
	uint64_t stx                          : 2;
	uint64_t reserved_18_23               : 6;
	uint64_t gfb                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t mwb                          : 1;
	uint64_t reserved_29_63               : 35;
#endif
	} cn63xx;
	struct cvmx_dfa_bist0_cn63xx          cn63xxp1;
	struct cvmx_dfa_bist0_cn63xx          cn66xx;
	struct cvmx_dfa_bist0_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_30_63               : 34;
	uint64_t mrp                          : 2;  /**< Bist Results for MRP RAM(s) (per-DLC)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t reserved_27_27               : 1;
	uint64_t gfb                          : 3;  /**< Bist Results for GFB RAM(s) (per-cluster)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t reserved_22_23               : 2;
	uint64_t stx2                         : 2;  /**< Bist Results for STX2 RAM(s)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t stx1                         : 2;  /**< Bist Results for STX1 RAM(s)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t stx                          : 2;  /**< Bist Results for STX0 RAM(s)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t reserved_14_15               : 2;
	uint64_t dtx2                         : 2;  /**< Bist Results for DTX2 RAM(s)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t dtx1                         : 2;  /**< Bist Results for DTX1 RAM(s)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t dtx                          : 2;  /**< Bist Results for DTX0 RAM(s)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t reserved_7_7                 : 1;
	uint64_t rdf                          : 3;  /**< Bist Results for RWB RAM(s) (per-cluster)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t reserved_3_3                 : 1;
	uint64_t pdb                          : 3;  /**< Bist Results for PDB RAM(s) (per-cluster)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
#else
	uint64_t pdb                          : 3;
	uint64_t reserved_3_3                 : 1;
	uint64_t rdf                          : 3;
	uint64_t reserved_7_7                 : 1;
	uint64_t dtx                          : 2;
	uint64_t dtx1                         : 2;
	uint64_t dtx2                         : 2;
	uint64_t reserved_14_15               : 2;
	uint64_t stx                          : 2;
	uint64_t stx1                         : 2;
	uint64_t stx2                         : 2;
	uint64_t reserved_22_23               : 2;
	uint64_t gfb                          : 3;
	uint64_t reserved_27_27               : 1;
	uint64_t mrp                          : 2;
	uint64_t reserved_30_63               : 34;
#endif
	} cn68xx;
	struct cvmx_dfa_bist0_cn68xx          cn68xxp1;
};
typedef union cvmx_dfa_bist0 cvmx_dfa_bist0_t;

/**
 * cvmx_dfa_bist1
 *
 * DFA_BIST1 = DFA Bist Status (Globals)
 *
 * Description:
 */
union cvmx_dfa_bist1 {
	uint64_t u64;
	struct cvmx_dfa_bist1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_21_63               : 43;
	uint64_t dlc1ram                      : 1;  /**< DLC1 Bist Results
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t dlc0ram                      : 1;  /**< DLC0 Bist Results
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t dc2ram3                      : 1;  /**< Cluster#2 Bist Results for RAM3 RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t dc2ram2                      : 1;  /**< Cluster#2 Bist Results for RAM2 RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t dc2ram1                      : 1;  /**< Cluster#2 Bist Results for RAM1 RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t dc1ram3                      : 1;  /**< Cluster#1 Bist Results for RAM3 RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t dc1ram2                      : 1;  /**< Cluster#1 Bist Results for RAM2 RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t dc1ram1                      : 1;  /**< Cluster#1 Bist Results for RAM1 RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t ram3                         : 1;  /**< Cluster#0 Bist Results for RAM3 RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t ram2                         : 1;  /**< Cluster#0 Bist Results for RAM2 RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t ram1                         : 1;  /**< Cluster#0 Bist Results for RAM1 RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t crq                          : 1;  /**< Bist Results for CRQ RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t gutv                         : 1;  /**< Bist Results for GUTV RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t reserved_7_7                 : 1;
	uint64_t gutp                         : 3;  /**< Bist Results for GUTP RAMs (per-cluster)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t ncd                          : 1;  /**< Bist Results for NCD RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t gif                          : 1;  /**< Bist Results for GIF RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t gib                          : 1;  /**< Bist Results for GIB RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t gfu                          : 1;  /**< Bist Results for GFU RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
#else
	uint64_t gfu                          : 1;
	uint64_t gib                          : 1;
	uint64_t gif                          : 1;
	uint64_t ncd                          : 1;
	uint64_t gutp                         : 3;
	uint64_t reserved_7_7                 : 1;
	uint64_t gutv                         : 1;
	uint64_t crq                          : 1;
	uint64_t ram1                         : 1;
	uint64_t ram2                         : 1;
	uint64_t ram3                         : 1;
	uint64_t dc1ram1                      : 1;
	uint64_t dc1ram2                      : 1;
	uint64_t dc1ram3                      : 1;
	uint64_t dc2ram1                      : 1;
	uint64_t dc2ram2                      : 1;
	uint64_t dc2ram3                      : 1;
	uint64_t dlc0ram                      : 1;
	uint64_t dlc1ram                      : 1;
	uint64_t reserved_21_63               : 43;
#endif
	} s;
	struct cvmx_dfa_bist1_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t dlc0ram                      : 1;  /**< DLC0 Bist Results
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t reserved_13_18               : 6;
	uint64_t ram3                         : 1;  /**< Cluster#0 Bist Results for RAM3 RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t ram2                         : 1;  /**< Cluster#0 Bist Results for RAM2 RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t ram1                         : 1;  /**< Cluster#0 Bist Results for RAM1 RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t crq                          : 1;  /**< Bist Results for CRQ RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t gutv                         : 1;  /**< Bist Results for GUTV RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t reserved_5_7                 : 3;
	uint64_t gutp                         : 1;  /**< Bist Results for GUTP RAMs
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t ncd                          : 1;  /**< Bist Results for NCD RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t gif                          : 1;  /**< Bist Results for GIF RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t gib                          : 1;  /**< Bist Results for GIB RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t gfu                          : 1;  /**< Bist Results for GFU RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
#else
	uint64_t gfu                          : 1;
	uint64_t gib                          : 1;
	uint64_t gif                          : 1;
	uint64_t ncd                          : 1;
	uint64_t gutp                         : 1;
	uint64_t reserved_5_7                 : 3;
	uint64_t gutv                         : 1;
	uint64_t crq                          : 1;
	uint64_t ram1                         : 1;
	uint64_t ram2                         : 1;
	uint64_t ram3                         : 1;
	uint64_t reserved_13_18               : 6;
	uint64_t dlc0ram                      : 1;
	uint64_t reserved_20_63               : 44;
#endif
	} cn61xx;
	struct cvmx_dfa_bist1_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_13_63               : 51;
	uint64_t ram3                         : 1;  /**< Bist Results for RAM3 RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t ram2                         : 1;  /**< Bist Results for RAM2 RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t ram1                         : 1;  /**< Bist Results for RAM1 RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t crq                          : 1;  /**< Bist Results for CRQ RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t gutv                         : 1;  /**< Bist Results for GUTV RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t reserved_5_7                 : 3;
	uint64_t gutp                         : 1;  /**< Bist Results for NCD RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t ncd                          : 1;  /**< Bist Results for NCD RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t gif                          : 1;  /**< Bist Results for GIF RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t gib                          : 1;  /**< Bist Results for GIB RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t gfu                          : 1;  /**< Bist Results for GFU RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
#else
	uint64_t gfu                          : 1;
	uint64_t gib                          : 1;
	uint64_t gif                          : 1;
	uint64_t ncd                          : 1;
	uint64_t gutp                         : 1;
	uint64_t reserved_5_7                 : 3;
	uint64_t gutv                         : 1;
	uint64_t crq                          : 1;
	uint64_t ram1                         : 1;
	uint64_t ram2                         : 1;
	uint64_t ram3                         : 1;
	uint64_t reserved_13_63               : 51;
#endif
	} cn63xx;
	struct cvmx_dfa_bist1_cn63xx          cn63xxp1;
	struct cvmx_dfa_bist1_cn63xx          cn66xx;
	struct cvmx_dfa_bist1_s               cn68xx;
	struct cvmx_dfa_bist1_s               cn68xxp1;
};
typedef union cvmx_dfa_bist1 cvmx_dfa_bist1_t;

/**
 * cvmx_dfa_bst0
 *
 * DFA_BST0 = DFA Bist Status
 *
 * Description:
 */
union cvmx_dfa_bst0 {
	uint64_t u64;
	struct cvmx_dfa_bst0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t rdf                          : 16; /**< Bist Results for RDF[3:0] RAM(s)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t pdf                          : 16; /**< Bist Results for PDF[3:0] RAM(s)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
#else
	uint64_t pdf                          : 16;
	uint64_t rdf                          : 16;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_dfa_bst0_s                cn31xx;
	struct cvmx_dfa_bst0_s                cn38xx;
	struct cvmx_dfa_bst0_s                cn38xxp2;
	struct cvmx_dfa_bst0_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t rdf                          : 4;  /**< Bist Results for RDF[3:0] RAM(s)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t reserved_4_15                : 12;
	uint64_t pdf                          : 4;  /**< Bist Results for PDF[3:0] RAM(s)
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
#else
	uint64_t pdf                          : 4;
	uint64_t reserved_4_15                : 12;
	uint64_t rdf                          : 4;
	uint64_t reserved_20_63               : 44;
#endif
	} cn58xx;
	struct cvmx_dfa_bst0_cn58xx           cn58xxp1;
};
typedef union cvmx_dfa_bst0 cvmx_dfa_bst0_t;

/**
 * cvmx_dfa_bst1
 *
 * DFA_BST1 = DFA Bist Status
 *
 * Description:
 */
union cvmx_dfa_bst1 {
	uint64_t u64;
	struct cvmx_dfa_bst1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_23_63               : 41;
	uint64_t crq                          : 1;  /**< Bist Results for CRQ RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t ifu                          : 1;  /**< Bist Results for IFU RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t gfu                          : 1;  /**< Bist Results for GFU RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t drf                          : 1;  /**< Bist Results for DRF RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t crf                          : 1;  /**< Bist Results for CRF RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t p0_bwb                       : 1;  /**< Bist Results for P0_BWB RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t p1_bwb                       : 1;  /**< Bist Results for P1_BWB RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t p0_brf                       : 8;  /**< Bist Results for P0_BRF RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t p1_brf                       : 8;  /**< Bist Results for P1_BRF RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
#else
	uint64_t p1_brf                       : 8;
	uint64_t p0_brf                       : 8;
	uint64_t p1_bwb                       : 1;
	uint64_t p0_bwb                       : 1;
	uint64_t crf                          : 1;
	uint64_t drf                          : 1;
	uint64_t gfu                          : 1;
	uint64_t ifu                          : 1;
	uint64_t crq                          : 1;
	uint64_t reserved_23_63               : 41;
#endif
	} s;
	struct cvmx_dfa_bst1_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_23_63               : 41;
	uint64_t crq                          : 1;  /**< Bist Results for CRQ RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t ifu                          : 1;  /**< Bist Results for IFU RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t gfu                          : 1;  /**< Bist Results for GFU RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t drf                          : 1;  /**< Bist Results for DRF RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t crf                          : 1;  /**< Bist Results for CRF RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t reserved_0_17                : 18;
#else
	uint64_t reserved_0_17                : 18;
	uint64_t crf                          : 1;
	uint64_t drf                          : 1;
	uint64_t gfu                          : 1;
	uint64_t ifu                          : 1;
	uint64_t crq                          : 1;
	uint64_t reserved_23_63               : 41;
#endif
	} cn31xx;
	struct cvmx_dfa_bst1_s                cn38xx;
	struct cvmx_dfa_bst1_s                cn38xxp2;
	struct cvmx_dfa_bst1_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_23_63               : 41;
	uint64_t crq                          : 1;  /**< Bist Results for CRQ RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t ifu                          : 1;  /**< Bist Results for IFU RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t gfu                          : 1;  /**< Bist Results for GFU RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t reserved_19_19               : 1;
	uint64_t crf                          : 1;  /**< Bist Results for CRF RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t p0_bwb                       : 1;  /**< Bist Results for P0_BWB RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t p1_bwb                       : 1;  /**< Bist Results for P1_BWB RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t p0_brf                       : 8;  /**< Bist Results for P0_BRF RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t p1_brf                       : 8;  /**< Bist Results for P1_BRF RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
#else
	uint64_t p1_brf                       : 8;
	uint64_t p0_brf                       : 8;
	uint64_t p1_bwb                       : 1;
	uint64_t p0_bwb                       : 1;
	uint64_t crf                          : 1;
	uint64_t reserved_19_19               : 1;
	uint64_t gfu                          : 1;
	uint64_t ifu                          : 1;
	uint64_t crq                          : 1;
	uint64_t reserved_23_63               : 41;
#endif
	} cn58xx;
	struct cvmx_dfa_bst1_cn58xx           cn58xxp1;
};
typedef union cvmx_dfa_bst1 cvmx_dfa_bst1_t;

/**
 * cvmx_dfa_cfg
 *
 * Specify the RSL base addresses for the block
 *
 *                  DFA_CFG = DFA Configuration
 *
 * Description:
 */
union cvmx_dfa_cfg {
	uint64_t u64;
	struct cvmx_dfa_cfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t nrpl_ena                     : 1;  /**< When set, allows the per-node replication feature to be
                                                         enabled.
                                                         In 36-bit mode: The IWORD0[31:30]=SNREPL field AND
                                                         bits [21:20] of the Next Node ptr are used in generating
                                                         the next node address (see OCTEON HRM - DFA Chapter for
                                                         psuedo-code of DTE next node address generation).
                                                         NOTE: When NRPL_ENA=1 and IWORD0[TY]=1(36b mode),
                                                         (regardless of IWORD0[NRPLEN]), the Resultant Word1+
                                                         [[47:44],[23:20]] = Next Node's [27:20] bits. This allows
                                                         SW to use the RESERVED bits of the final node for SW
                                                         caching. Also, if required, SW will use [22:21]=Node
                                                         Replication to re-start the same graph walk(if graph
                                                         walk prematurely terminated (ie: DATA_GONE).
                                                         In 18-bit mode: The IWORD0[31:30]=SNREPL field AND
                                                         bit [16:14] of the Next Node ptr are used in generating
                                                         the next node address (see OCTEON HRM - DFA Chapter for
                                                         psuedo-code of DTE next node address generation).
                                                         If (IWORD0[NREPLEN]=1 and DFA_CFG[NRPL_ENA]=1) [
                                                            If next node ptr[16] is set [
                                                              next node ptr[15:14] indicates the next node repl
                                                              next node ptr[13:0]  indicates the position of the
                                                                 node relative to the first normal node (i.e.
                                                                 IWORD3[Msize] must be added to get the final node)
                                                            ]
                                                            else If next node ptr[16] is not set [
                                                              next node ptr[15:0] indicates the next node id
                                                              next node repl = 0
                                                            ]
                                                         ]
                                                         NOTE: For 18b node replication, MAX node space=64KB(2^16)
                                                         is used in detecting terminal node space(see HRM for full
                                                         description).
                                                         NOTE: The DFA graphs MUST BE built/written to DFA LLM memory
                                                         aware of the "per-node" replication. */
	uint64_t nxor_ena                     : 1;  /**< When set, allows the DTE Instruction IWORD0[NXOREN]
                                                         to be used to enable/disable the per-node address 'scramble'
                                                         of the LLM address to lessen the effects of bank conflicts.
                                                         If IWORD0[NXOREN] is also set, then:
                                                         In 36-bit mode: The node_Id[7:0] 8-bit value is XORed
                                                         against the LLM address addr[9:2].
                                                         In 18-bit mode: The node_id[6:0] 7-bit value is XORed
                                                         against the LLM address addr[8:2]. (note: we don't address
                                                         scramble outside the mode's node space).
                                                         NOTE: The DFA graphs MUST BE built/written to DFA LLM memory
                                                         aware of the "per-node" address scramble.
                                                         NOTE: The address 'scramble' ocurs for BOTH DFA LLM graph
                                                         read/write operations. */
	uint64_t gxor_ena                     : 1;  /**< When set, the DTE Instruction IWORD0[GXOR]
                                                         field is used to 'scramble' the LLM address
                                                         to lessen the effects of bank conflicts.
                                                         In 36-bit mode: The GXOR[7:0] 8-bit value is XORed
                                                         against the LLM address addr[9:2].
                                                         In 18-bit mode: GXOR[6:0] 7-bit value is XORed against
                                                         the LLM address addr[8:2]. (note: we don't address
                                                         scramble outside the mode's node space)
                                                         NOTE: The DFA graphs MUST BE built/written to DFA LLM memory
                                                         aware of the "per-graph" address scramble.
                                                         NOTE: The address 'scramble' ocurs for BOTH DFA LLM graph
                                                         read/write operations. */
	uint64_t sarb                         : 1;  /**< DFA Source Arbiter Mode
                                                         Selects the arbitration mode used to select DFA
                                                         requests issued from either CP2 or the DTE (NCB-CSR
                                                         or DFA HW engine).
                                                            - 0: Fixed Priority [Highest=CP2, Lowest=DTE]
                                                            - 1: Round-Robin
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
#else
	uint64_t sarb                         : 1;
	uint64_t gxor_ena                     : 1;
	uint64_t nxor_ena                     : 1;
	uint64_t nrpl_ena                     : 1;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_dfa_cfg_s                 cn38xx;
	struct cvmx_dfa_cfg_cn38xxp2 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t sarb                         : 1;  /**< DFA Source Arbiter Mode
                                                         Selects the arbitration mode used to select DFA
                                                         requests issued from either CP2 or the DTE (NCB-CSR
                                                         or DFA HW engine).
                                                            - 0: Fixed Priority [Highest=CP2, Lowest=DTE]
                                                            - 1: Round-Robin
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
#else
	uint64_t sarb                         : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} cn38xxp2;
	struct cvmx_dfa_cfg_s                 cn58xx;
	struct cvmx_dfa_cfg_s                 cn58xxp1;
};
typedef union cvmx_dfa_cfg cvmx_dfa_cfg_t;

/**
 * cvmx_dfa_config
 *
 * Specify the RSL base addresses for the block
 *
 *                  DFA_CONFIG = DFA Configuration Register
 *
 * Description:
 */
union cvmx_dfa_config {
	uint64_t u64;
	struct cvmx_dfa_config_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_11_63               : 53;
	uint64_t dlcclear_bist                : 1;  /**< When DLCSTART_BIST is written 0->1, if DLCCLEAR_BIST=1, all
                                                         previous DLC BiST state is cleared.
                                                         NOTES:
                                                         1) DLCCLEAR_BIST must be written to 1 before DLCSTART_BIST
                                                         is written to 1 udsing a separate CSR write.
                                                         2) DLCCLEAR_BIST must not be changed after writing DLCSTART_BIST
                                                         0->1 until the BIST operation completes. */
	uint64_t dlcstart_bist                : 1;  /**< When software writes DLCSTART_BIST=0->1, a BiST is executed
                                                         for the DLC sub-block RAMs which contains DCLK domain
                                                         asynchronous RAMs.
                                                         NOTES:
                                                         1) This bit should only be written after DCLK has been enabled
                                                         by software and is stable.
                                                         (see LMC initialization routine for details on how to enable
                                                         the DDR3 memory (DCLK) - which requires LMC PLL init, clock
                                                         divider and proper DLL initialization sequence). */
	uint64_t repl_ena                     : 1;  /**< Replication Mode Enable
                                                         *** o63-P2 NEW ***
                                                         When set, enables replication mode performance enhancement
                                                         feature. This enables the DFA to communicate address
                                                         replication information during memory references to the
                                                         memory controller.
                                                         For o63-P2: This is used by the memory controller
                                                         to support graph data in multiple banks (or bank sets), so that
                                                         the least full bank can be selected to minimize the effects of
                                                         DDR3 bank conflicts (ie: tRC=row cycle time).
                                                         For o68: This is used by the memory controller to support graph
                                                         data in multiple ports (or port sets), so that the least full
                                                         port can be selected to minimize latency effects.
                                                         SWNOTE: Using this mode requires the DFA SW compiler and DFA
                                                         driver to be aware of the address replication changes.
                                                         This involves changes to the MLOAD/GWALK DFA instruction format
                                                         (see: IWORD2.SREPL), as well as changes to node arc and metadata
                                                         definitions which now support an additional REPL field.
                                                         When clear, replication mode is disabled, and DFA will interpret
                                                         DFA instructions and node-arc formats which DO NOT have
                                                         address replication information. */
	uint64_t clmskcrip                    : 4;  /**< Cluster Cripple Mask
                                                         A one in each bit of the mask represents which DTE cluster to
                                                         cripple.
                                                         NOTE: o63 has only a single Cluster (therefore CLMSKCRIP[0]
                                                         is the only bit used.
                                                         o2 has 4 clusters, where all CLMSKCRIP mask bits are used.
                                                         SWNOTE: The MIO_FUS___DFA_CLMASK_CRIPPLE[3:0] fuse bits will
                                                         be forced into this register at reset. Any fuse bits that
                                                         contain '1' will be disallowed during a write and will always
                                                         be read as '1'. */
	uint64_t cldtecrip                    : 3;  /**< Encoding which represents \#of DTEs to cripple for each
                                                         cluster. Typically DTE_CLCRIP=0 which enables all DTEs
                                                         within each cluster. However, when the DFA performance
                                                         counters are used, SW may want to limit the \#of DTEs
                                                         per cluster available, as there are only 4 parallel
                                                         performance counters.
                                                            DTE_CLCRIP | \#DTEs crippled(per cluster)
                                                         ------------+-----------------------------
                                                                0    |  0      DTE[15:0]:ON
                                                                1    |  1/2    DTE[15:8]:OFF  /DTE[7:0]:ON
                                                                2    |  1/4    DTE[15:12]:OFF /DTE[11:0]:ON
                                                                3    |  3/4    DTE[15:4]:OFF  /DTE[3:0]:ON
                                                                4    |  1/8    DTE[15:14]:OFF /DTE[13:0]:ON
                                                                5    |  5/8    DTE[15:6]:OFF  /DTE[5:0]:ON
                                                                6    |  3/8    DTE[15:10]:OFF /DTE[9:0]:ON
                                                                7    |  7/8    DTE[15:2]:OFF  /DTE[1:0]:ON
                                                         NOTE: Higher numbered DTEs are crippled first. For instance,
                                                         on o63 (with 16 DTEs/cluster), if DTE_CLCRIP=1(1/2), then
                                                         DTE#s [15:8] within the cluster are crippled and only
                                                         DTE#s [7:0] are available.
                                                         IMPNOTE: The encodings are done in such a way as to later
                                                         be used with fuses (for future o2 revisions which will disable
                                                         some \#of DTEs). Blowing a fuse has the effect that there will
                                                         always be fewer DTEs available. [ie: we never want a customer
                                                         to blow additional fuses to get more DTEs].
                                                         SWNOTE: The MIO_FUS___DFA_NUMDTE_CRIPPLE[2:0] fuse bits will
                                                         be forced into this register at reset. Any fuse bits that
                                                         contain '1' will be disallowed during a write and will always
                                                         be read as '1'. */
	uint64_t dteclkdis                    : 1;  /**< DFA Clock Disable Source
                                                         When SET, the DFA clocks for DTE(thread engine)
                                                         operation are disabled (to conserve overall chip clocking
                                                         power when the DFA function is not used).
                                                         NOTE: When SET, SW MUST NEVER issue NCB-Direct CSR
                                                         operations to the DFA (will result in NCB Bus Timeout
                                                         errors).
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization.
                                                         SWNOTE: The MIO_FUS___DFA_DTE_DISABLE fuse bit will
                                                         be forced into this register at reset. If the fuse bit
                                                         contains '1', writes to DTECLKDIS are disallowed and
                                                         will always be read as '1'. */
#else
	uint64_t dteclkdis                    : 1;
	uint64_t cldtecrip                    : 3;
	uint64_t clmskcrip                    : 4;
	uint64_t repl_ena                     : 1;
	uint64_t dlcstart_bist                : 1;
	uint64_t dlcclear_bist                : 1;
	uint64_t reserved_11_63               : 53;
#endif
	} s;
	struct cvmx_dfa_config_s              cn61xx;
	struct cvmx_dfa_config_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t repl_ena                     : 1;  /**< Replication Mode Enable
                                                         *** o63-P2 NEW ***
                                                         When set, enables replication mode performance enhancement
                                                         feature. This enables the DFA to communicate address
                                                         replication information during memory references to the DFM
                                                         (memory controller). This in turn is used by the DFM to support
                                                         graph data in multiple banks (or bank sets), so that the least
                                                         full bank can be selected to minimize the effects of DDR3 bank
                                                         conflicts (ie: tRC=row cycle time).
                                                         SWNOTE: Using this mode requires the DFA SW compiler and DFA
                                                         driver to be aware of the o63-P2 address replication changes.
                                                         This involves changes to the MLOAD/GWALK DFA instruction format
                                                         (see: IWORD2.SREPL), as well as changes to node arc and metadata
                                                         definitions which now support an additional REPL field.
                                                         When clear, replication mode is disabled, and DFA will interpret
                                                         o63-P1 DFA instructions and node-arc formats which DO NOT have
                                                         address replication information. */
	uint64_t clmskcrip                    : 4;  /**< Cluster Cripple Mask
                                                         A one in each bit of the mask represents which DTE cluster to
                                                         cripple.
                                                         NOTE: o63 has only a single Cluster (therefore CLMSKCRIP[0]
                                                         is the only bit used.
                                                         o2 has 4 clusters, where all CLMSKCRIP mask bits are used.
                                                         SWNOTE: The MIO_FUS___DFA_CLMASK_CRIPPLE[3:0] fuse bits will
                                                         be forced into this register at reset. Any fuse bits that
                                                         contain '1' will be disallowed during a write and will always
                                                         be read as '1'. */
	uint64_t cldtecrip                    : 3;  /**< Encoding which represents \#of DTEs to cripple for each
                                                         cluster. Typically DTE_CLCRIP=0 which enables all DTEs
                                                         within each cluster. However, when the DFA performance
                                                         counters are used, SW may want to limit the \#of DTEs
                                                         per cluster available, as there are only 4 parallel
                                                         performance counters.
                                                            DTE_CLCRIP | \#DTEs crippled(per cluster)
                                                         ------------+-----------------------------
                                                                0    |  0      DTE[15:0]:ON
                                                                1    |  1/2    DTE[15:8]:OFF  /DTE[7:0]:ON
                                                                2    |  1/4    DTE[15:12]:OFF /DTE[11:0]:ON
                                                                3    |  3/4    DTE[15:4]:OFF  /DTE[3:0]:ON
                                                                4    |  1/8    DTE[15:14]:OFF /DTE[13:0]:ON
                                                                5    |  5/8    DTE[15:6]:OFF  /DTE[5:0]:ON
                                                                6    |  3/8    DTE[15:10]:OFF /DTE[9:0]:ON
                                                                7    |  7/8    DTE[15:2]:OFF  /DTE[1:0]:ON
                                                         NOTE: Higher numbered DTEs are crippled first. For instance,
                                                         on o63 (with 16 DTEs/cluster), if DTE_CLCRIP=1(1/2), then
                                                         DTE#s [15:8] within the cluster are crippled and only
                                                         DTE#s [7:0] are available.
                                                         IMPNOTE: The encodings are done in such a way as to later
                                                         be used with fuses (for future o2 revisions which will disable
                                                         some \#of DTEs). Blowing a fuse has the effect that there will
                                                         always be fewer DTEs available. [ie: we never want a customer
                                                         to blow additional fuses to get more DTEs].
                                                         SWNOTE: The MIO_FUS___DFA_NUMDTE_CRIPPLE[2:0] fuse bits will
                                                         be forced into this register at reset. Any fuse bits that
                                                         contain '1' will be disallowed during a write and will always
                                                         be read as '1'. */
	uint64_t dteclkdis                    : 1;  /**< DFA Clock Disable Source
                                                         When SET, the DFA clocks for DTE(thread engine)
                                                         operation are disabled (to conserve overall chip clocking
                                                         power when the DFA function is not used).
                                                         NOTE: When SET, SW MUST NEVER issue NCB-Direct CSR
                                                         operations to the DFA (will result in NCB Bus Timeout
                                                         errors).
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization.
                                                         SWNOTE: The MIO_FUS___DFA_DTE_DISABLE fuse bit will
                                                         be forced into this register at reset. If the fuse bit
                                                         contains '1', writes to DTECLKDIS are disallowed and
                                                         will always be read as '1'. */
#else
	uint64_t dteclkdis                    : 1;
	uint64_t cldtecrip                    : 3;
	uint64_t clmskcrip                    : 4;
	uint64_t repl_ena                     : 1;
	uint64_t reserved_9_63                : 55;
#endif
	} cn63xx;
	struct cvmx_dfa_config_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t clmskcrip                    : 4;  /**< Cluster Cripple Mask
                                                         A one in each bit of the mask represents which DTE cluster to
                                                         cripple.
                                                         NOTE: o63 has only a single Cluster (therefore CLMSKCRIP[0]
                                                         is the only bit used.
                                                         o2 has 4 clusters, where all CLMSKCRIP mask bits are used.
                                                         SWNOTE: The MIO_FUS___DFA_CLMASK_CRIPPLE[3:0] fuse bits will
                                                         be forced into this register at reset. Any fuse bits that
                                                         contain '1' will be disallowed during a write and will always
                                                         be read as '1'. */
	uint64_t cldtecrip                    : 3;  /**< Encoding which represents \#of DTEs to cripple for each
                                                         cluster. Typically DTE_CLCRIP=0 which enables all DTEs
                                                         within each cluster. However, when the DFA performance
                                                         counters are used, SW may want to limit the \#of DTEs
                                                         per cluster available, as there are only 4 parallel
                                                         performance counters.
                                                            DTE_CLCRIP | \#DTEs crippled(per cluster)
                                                         ------------+-----------------------------
                                                                0    |  0      DTE[15:0]:ON
                                                                1    |  1/2    DTE[15:8]:OFF  /DTE[7:0]:ON
                                                                2    |  1/4    DTE[15:12]:OFF /DTE[11:0]:ON
                                                                3    |  3/4    DTE[15:4]:OFF  /DTE[3:0]:ON
                                                                4    |  1/8    DTE[15:14]:OFF /DTE[13:0]:ON
                                                                5    |  5/8    DTE[15:6]:OFF  /DTE[5:0]:ON
                                                                6    |  3/8    DTE[15:10]:OFF /DTE[9:0]:ON
                                                                7    |  7/8    DTE[15:2]:OFF  /DTE[1:0]:ON
                                                         NOTE: Higher numbered DTEs are crippled first. For instance,
                                                         on o63 (with 16 DTEs/cluster), if DTE_CLCRIP=1(1/2), then
                                                         DTE#s [15:8] within the cluster are crippled and only
                                                         DTE#s [7:0] are available.
                                                         IMPNOTE: The encodings are done in such a way as to later
                                                         be used with fuses (for future o2 revisions which will disable
                                                         some \#of DTEs). Blowing a fuse has the effect that there will
                                                         always be fewer DTEs available. [ie: we never want a customer
                                                         to blow additional fuses to get more DTEs].
                                                         SWNOTE: The MIO_FUS___DFA_NUMDTE_CRIPPLE[2:0] fuse bits will
                                                         be forced into this register at reset. Any fuse bits that
                                                         contain '1' will be disallowed during a write and will always
                                                         be read as '1'. */
	uint64_t dteclkdis                    : 1;  /**< DFA Clock Disable Source
                                                         When SET, the DFA clocks for DTE(thread engine)
                                                         operation are disabled (to conserve overall chip clocking
                                                         power when the DFA function is not used).
                                                         NOTE: When SET, SW MUST NEVER issue NCB-Direct CSR
                                                         operations to the DFA (will result in NCB Bus Timeout
                                                         errors).
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization.
                                                         SWNOTE: The MIO_FUS___DFA_DTE_DISABLE fuse bit will
                                                         be forced into this register at reset. If the fuse bit
                                                         contains '1', writes to DTECLKDIS are disallowed and
                                                         will always be read as '1'. */
#else
	uint64_t dteclkdis                    : 1;
	uint64_t cldtecrip                    : 3;
	uint64_t clmskcrip                    : 4;
	uint64_t reserved_8_63                : 56;
#endif
	} cn63xxp1;
	struct cvmx_dfa_config_cn63xx         cn66xx;
	struct cvmx_dfa_config_s              cn68xx;
	struct cvmx_dfa_config_s              cn68xxp1;
};
typedef union cvmx_dfa_config cvmx_dfa_config_t;

/**
 * cvmx_dfa_control
 *
 * DFA_CONTROL = DFA Control Register
 *
 * Description:
 */
union cvmx_dfa_control {
	uint64_t u64;
	struct cvmx_dfa_control_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_12_63               : 52;
	uint64_t sbdnum                       : 6;  /**< SBD Debug Entry#
                                                         *FOR INTERNAL USE ONLY*
                                                         DFA Scoreboard debug control
                                                         Selects which one of 48 DFA Scoreboard entries is
                                                         latched into the DFA_SBD_DBG[0-3] registers. */
	uint64_t sbdlck                       : 1;  /**< DFA Scoreboard LOCK Strobe
                                                         *FOR INTERNAL USE ONLY*
                                                         DFA Scoreboard debug control
                                                         When written with a '1', the DFA Scoreboard Debug
                                                         registers (DFA_SBD_DBG[0-3]) are all locked down.
                                                         This allows SW to lock down the contents of the entire
                                                         SBD for a single instant in time. All subsequent reads
                                                         of the DFA scoreboard registers will return the data
                                                         from that instant in time. */
	uint64_t reserved_3_4                 : 2;
	uint64_t pmode                        : 1;  /**< NCB-NRP Arbiter Mode
                                                         (0=Fixed Priority [LP=WQF,DFF,HP=RGF]/1=RR
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t qmode                        : 1;  /**< NCB-NRQ Arbiter Mode
                                                         (0=Fixed Priority [LP=IRF,RWF,PRF,HP=GRF]/1=RR
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t imode                        : 1;  /**< NCB-Inbound Arbiter
                                                         (0=FP [LP=NRQ,HP=NRP], 1=RR)
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
#else
	uint64_t imode                        : 1;
	uint64_t qmode                        : 1;
	uint64_t pmode                        : 1;
	uint64_t reserved_3_4                 : 2;
	uint64_t sbdlck                       : 1;
	uint64_t sbdnum                       : 6;
	uint64_t reserved_12_63               : 52;
#endif
	} s;
	struct cvmx_dfa_control_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_10_63               : 54;
	uint64_t sbdnum                       : 4;  /**< SBD Debug Entry#
                                                         *FOR INTERNAL USE ONLY*
                                                         DFA Scoreboard debug control
                                                         Selects which one of 16 DFA Scoreboard entries is
                                                         latched into the DFA_SBD_DBG[0-3] registers. */
	uint64_t sbdlck                       : 1;  /**< DFA Scoreboard LOCK Strobe
                                                         *FOR INTERNAL USE ONLY*
                                                         DFA Scoreboard debug control
                                                         When written with a '1', the DFA Scoreboard Debug
                                                         registers (DFA_SBD_DBG[0-3]) are all locked down.
                                                         This allows SW to lock down the contents of the entire
                                                         SBD for a single instant in time. All subsequent reads
                                                         of the DFA scoreboard registers will return the data
                                                         from that instant in time. */
	uint64_t reserved_3_4                 : 2;
	uint64_t pmode                        : 1;  /**< NCB-NRP Arbiter Mode
                                                         (0=Fixed Priority [LP=WQF,DFF,HP=RGF]/1=RR
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t qmode                        : 1;  /**< NCB-NRQ Arbiter Mode
                                                         (0=Fixed Priority [LP=IRF,RWF,PRF,HP=GRF]/1=RR
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t imode                        : 1;  /**< NCB-Inbound Arbiter
                                                         (0=FP [LP=NRQ,HP=NRP], 1=RR)
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
#else
	uint64_t imode                        : 1;
	uint64_t qmode                        : 1;
	uint64_t pmode                        : 1;
	uint64_t reserved_3_4                 : 2;
	uint64_t sbdlck                       : 1;
	uint64_t sbdnum                       : 4;
	uint64_t reserved_10_63               : 54;
#endif
	} cn61xx;
	struct cvmx_dfa_control_cn61xx        cn63xx;
	struct cvmx_dfa_control_cn61xx        cn63xxp1;
	struct cvmx_dfa_control_cn61xx        cn66xx;
	struct cvmx_dfa_control_s             cn68xx;
	struct cvmx_dfa_control_s             cn68xxp1;
};
typedef union cvmx_dfa_control cvmx_dfa_control_t;

/**
 * cvmx_dfa_dbell
 *
 * DFA_DBELL = DFA Doorbell Register
 *
 * Description:
 *  NOTE: To write to the DFA_DBELL register, a device would issue an IOBST directed at the DFA with addr[34:33]=2'b00.
 *        To read the DFA_DBELL register, a device would issue an IOBLD64 directed at the DFA with addr[34:33]=2'b00.
 *
 *  NOTE: If DFA_CONFIG[DTECLKDIS]=1 (DFA-DTE clocks disabled), reads/writes to the DFA_DBELL register do not take effect.
 *  NOTE: If FUSE[TBD]="DFA DTE disable" is blown, reads/writes to the DFA_DBELL register do not take effect.
 */
union cvmx_dfa_dbell {
	uint64_t u64;
	struct cvmx_dfa_dbell_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t dbell                        : 20; /**< Represents the cumulative total of pending
                                                         DFA instructions which SW has previously written
                                                         into the DFA Instruction FIFO (DIF) in main memory.
                                                         Each DFA instruction contains a fixed size 32B
                                                         instruction word which is executed by the DFA HW.
                                                         The DBL register can hold up to 1M-1 (2^20-1)
                                                         pending DFA instruction requests.
                                                         During a read (by SW), the 'most recent' contents
                                                         of the DFA_DBELL register are returned at the time
                                                         the NCB-INB bus is driven.
                                                         NOTE: Since DFA HW updates this register, its
                                                         contents are unpredictable in SW. */
#else
	uint64_t dbell                        : 20;
	uint64_t reserved_20_63               : 44;
#endif
	} s;
	struct cvmx_dfa_dbell_s               cn31xx;
	struct cvmx_dfa_dbell_s               cn38xx;
	struct cvmx_dfa_dbell_s               cn38xxp2;
	struct cvmx_dfa_dbell_s               cn58xx;
	struct cvmx_dfa_dbell_s               cn58xxp1;
	struct cvmx_dfa_dbell_s               cn61xx;
	struct cvmx_dfa_dbell_s               cn63xx;
	struct cvmx_dfa_dbell_s               cn63xxp1;
	struct cvmx_dfa_dbell_s               cn66xx;
	struct cvmx_dfa_dbell_s               cn68xx;
	struct cvmx_dfa_dbell_s               cn68xxp1;
};
typedef union cvmx_dfa_dbell cvmx_dfa_dbell_t;

/**
 * cvmx_dfa_ddr2_addr
 *
 * DFA_DDR2_ADDR = DFA DDR2  fclk-domain Memory Address Config Register
 *
 *
 * Description: The following registers are used to compose the DFA's DDR2 address into ROW/COL/BNK
 *              etc.
 */
union cvmx_dfa_ddr2_addr {
	uint64_t u64;
	struct cvmx_dfa_ddr2_addr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t rdimm_ena                    : 1;  /**< If there is a need to insert a register chip on the
                                                         system (the equivalent of a registered DIMM) to
                                                         provide better setup for the command and control bits
                                                         turn this mode on.
                                                             RDIMM_ENA
                                                                0           Registered Mode OFF
                                                                1           Registered Mode ON */
	uint64_t num_rnks                     : 2;  /**< NUM_RNKS is programmed based on how many ranks there
                                                         are in the system. This needs to be programmed correctly
                                                         regardless of whether we are in RNK_LO mode or not.
                                                            NUM_RNKS     \# of Ranks
                                                              0              1
                                                              1              2
                                                              2              4
                                                              3              RESERVED */
	uint64_t rnk_lo                       : 1;  /**< When this mode is turned on, consecutive addresses
                                                         outside the bank boundary
                                                         are programmed to go to different ranks in order to
                                                         minimize bank conflicts. It is useful in 4-bank DDR2
                                                         parts based memory to extend out the \#physical banks
                                                         available and minimize bank conflicts.
                                                         On 8 bank ddr2 parts, this mode is not very useful
                                                         because this mode does come with
                                                         a penalty which is that every successive reads that
                                                         cross rank boundary will need a 1 cycle bubble
                                                         inserted to prevent bus turnaround conflicts.
                                                            RNK_LO
                                                             0      - OFF
                                                             1      - ON */
	uint64_t num_colrows                  : 3;  /**< NUM_COLROWS    is used to set the MSB of the ROW_ADDR
                                                         and the LSB of RANK address when not in RNK_LO mode.
                                                         Calculate the sum of \#COL and \#ROW and program the
                                                         controller appropriately
                                                            RANK_LSB        \#COLs + \#ROWs
                                                            ------------------------------
                                                             - 000:                   22
                                                             - 001:                   23
                                                             - 010:                   24
                                                             - 011:                   25
                                                            - 100-111:             RESERVED */
	uint64_t num_cols                     : 2;  /**< The Long word address that the controller receives
                                                         needs to be converted to Row, Col, Rank and Bank
                                                         addresses depending on the memory part's micro arch.
                                                         NUM_COL tells the controller how many colum bits
                                                         there are and the controller uses this info to map
                                                         the LSB of the row address
                                                             - 00: num_cols = 9
                                                             - 01: num_cols = 10
                                                             - 10: num_cols = 11
                                                             - 11: RESERVED */
#else
	uint64_t num_cols                     : 2;
	uint64_t num_colrows                  : 3;
	uint64_t rnk_lo                       : 1;
	uint64_t num_rnks                     : 2;
	uint64_t rdimm_ena                    : 1;
	uint64_t reserved_9_63                : 55;
#endif
	} s;
	struct cvmx_dfa_ddr2_addr_s           cn31xx;
};
typedef union cvmx_dfa_ddr2_addr cvmx_dfa_ddr2_addr_t;

/**
 * cvmx_dfa_ddr2_bus
 *
 * DFA_DDR2_BUS = DFA DDR Bus Activity Counter
 *
 *
 * Description: This counter counts \# cycles that the memory bus is doing a read/write/command
 *              Useful to benchmark the bus utilization as a ratio of
 *              \#Cycles of Data Transfer/\#Cycles since init or
 *              \#Cycles of Data Transfer/\#Cycles that memory controller is active
 */
union cvmx_dfa_ddr2_bus {
	uint64_t u64;
	struct cvmx_dfa_ddr2_bus_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_47_63               : 17;
	uint64_t bus_cnt                      : 47; /**< Counter counts the \# cycles of Data transfer */
#else
	uint64_t bus_cnt                      : 47;
	uint64_t reserved_47_63               : 17;
#endif
	} s;
	struct cvmx_dfa_ddr2_bus_s            cn31xx;
};
typedef union cvmx_dfa_ddr2_bus cvmx_dfa_ddr2_bus_t;

/**
 * cvmx_dfa_ddr2_cfg
 *
 * DFA_DDR2_CFG = DFA DDR2 fclk-domain Memory Configuration \#0 Register
 *
 * Description:
 */
union cvmx_dfa_ddr2_cfg {
	uint64_t u64;
	struct cvmx_dfa_ddr2_cfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_41_63               : 23;
	uint64_t trfc                         : 5;  /**< Establishes tRFC(from DDR2 data sheets) in \# of
                                                         4 fclk intervals.
                                                         General Equation:
                                                         TRFC(csr) = ROUNDUP[tRFC(data-sheet-ns)/(4 * fclk(ns))]
                                                         Example:
                                                            tRFC(data-sheet-ns) = 127.5ns
                                                            Operational Frequency: 533MHz DDR rate
                                                                [fclk=266MHz(3.75ns)]
                                                         Then:
                                                            TRFC(csr) = ROUNDUP[127.5ns/(4 * 3.75ns)]
                                                                      = 9 */
	uint64_t mrs_pgm                      : 1;  /**< When clear, the HW initialization sequence fixes
                                                         some of the *MRS register bit definitions.
                                                            EMRS:
                                                              A[14:13] = 0 RESERVED
                                                              A[12] = 0    Output Buffers Enabled (FIXED)
                                                              A[11] = 0    RDQS Disabled (FIXED)
                                                              A[10] = 0    DQSn Enabled (FIXED)
                                                              A[9:7] = 0   OCD Not supported (FIXED)
                                                              A[6] = 0     RTT Disabled (FIXED)
                                                              A[5:3]=DFA_DDR2_TMG[ADDLAT] (if DFA_DDR2_TMG[POCAS]=1)
                                                                            Additive LATENCY (Programmable)
                                                              A[2]=0       RTT Disabled (FIXED)
                                                              A[1]=DFA_DDR2_TMG[DIC] (Programmable)
                                                              A[0] = 0     DLL Enabled (FIXED)
                                                            MRS:
                                                              A[14:13] = 0 RESERVED
                                                              A[12] = 0    Fast Active Power Down Mode (FIXED)
                                                              A[11:9] = DFA_DDR2_TMG[TWR](Programmable)
                                                              A[8] = 1     DLL Reset (FIXED)
                                                              A[7] = 0     Test Mode (FIXED)
                                                              A[6:4]=DFA_DDR2_TMG[CASLAT] CAS LATENCY (Programmable)
                                                              A[3] = 0     Burst Type(must be 0:Sequential) (FIXED)
                                                              A[2:0] = 2   Burst Length=4 (must be 0:Sequential) (FIXED)
                                                         When set, the HW initialization sequence sources
                                                         the DFA_DDR2_MRS, DFA_DDR2_EMRS registers which are
                                                         driven onto the DFA_A[] pins. (this allows the MRS/EMRS
                                                         fields to be completely programmable - however care
                                                         must be taken by software).
                                                         This mode is useful for customers who wish to:
                                                            1) override the FIXED definitions(above), or
                                                            2) Use a "clamshell mode" of operation where the
                                                               address bits(per rank) are swizzled on the
                                                               board to reduce stub lengths for optimal
                                                               frequency operation.
                                                         Use this in combination with DFA_DDR2_CFG[RNK_MSK]
                                                         to specify the INIT sequence for each of the 4
                                                         supported ranks. */
	uint64_t fpip                         : 3;  /**< Early Fill Programmable Pipe [\#fclks]
                                                         This field dictates the \#fclks prior to the arrival
                                                         of fill data(in fclk domain), to start the 'early' fill
                                                         command pipe (in the eclk domain) so as to minimize the
                                                         overall fill latency.
                                                         The programmable early fill command signal is synchronized
                                                         into the eclk domain, where it is used to pull data out of
                                                         asynchronous RAM as fast as possible.
                                                         NOTE: A value of FPIP=0 is the 'safest' setting and will
                                                         result in the early fill command pipe starting in the
                                                         same cycle as the fill data.
                                                         General Equation: (for FPIP)
                                                             FPIP <= MIN[6, (ROUND_DOWN[6/EF_RATIO] + 1)]
                                                         where:
                                                           EF_RATIO = ECLK/FCLK Ratio [eclk(MHz)/fclk(MHz)]
                                                         Example: FCLK=200MHz/ECLK=600MHz
                                                            FPIP = MIN[6, (ROUND_DOWN[6/(600/200))] + 1)]
                                                            FPIP <= 3 */
	uint64_t reserved_29_31               : 3;
	uint64_t ref_int                      : 13; /**< Refresh Interval (represented in \#of fclk
                                                         increments).
                                                         Each refresh interval will generate a single
                                                         auto-refresh command sequence which implicitly targets
                                                         all banks within the device:
                                                         Example: For fclk=200MHz(5ns)/400MHz(DDR):
                                                           trefint(ns) = [tREFI(max)=3.9us = 3900ns [datasheet]
                                                           REF_INT = ROUND_DOWN[(trefint/fclk)]
                                                                   = ROUND_DOWN[(3900ns/5ns)]
                                                                   = 780 fclks (0x30c)
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t reserved_14_15               : 2;
	uint64_t tskw                         : 2;  /**< Board Skew (represented in \#fclks)
                                                         Represents additional board skew of DQ/DQS.
                                                             - 00: board-skew = 0 fclk
                                                             - 01: board-skew = 1 fclk
                                                             - 10: board-skew = 2 fclk
                                                             - 11: board-skew = 3 fclk
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t rnk_msk                      : 4;  /**< Controls the CS_N[3:0] during a) a HW Initialization
                                                         sequence (triggered by DFA_DDR2_CFG[INIT]) or
                                                         b) during a normal refresh sequence. If
                                                         the RNK_MSK[x]=1, the corresponding CS_N[x] is driven.
                                                         NOTE: This is required for DRAM used in a
                                                         clamshell configuration, since the address lines
                                                         carry Mode Register write data that is unique
                                                         per rank(or clam). In a clamshell configuration,
                                                         the N3K DFA_A[x] pin may be tied into Clam#0's A[x]
                                                         and also into Clam#1's 'mirrored' address bit A[y]
                                                         (eg: Clam0 sees A[5] and Clam1 sees A[15]).
                                                         To support clamshell designs, SW must initiate
                                                         separate HW init sequences each unique rank address
                                                         mapping. Before each HW init sequence is triggered,
                                                         SW must preload the DFA_DDR2_MRS/EMRS registers with
                                                         the data that will be driven onto the A[14:0] wires
                                                         during the EMRS/MRS mode register write(s).
                                                         NOTE: After the final HW initialization sequence has
                                                         been triggered, SW must wait 64K eclks before writing
                                                         the RNK_MSK[3:0] field = 3'b1111 (so that CS_N[3:0]
                                                         is driven during refresh sequences in normal operation.
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t silo_qc                      : 1;  /**< Enables Quarter Cycle move of the Rd sampling window */
	uint64_t silo_hc                      : 1;  /**< A combination of SILO_HC, SILO_QC and TSKW
                                                         specifies the positioning of the sampling strobe
                                                         when receiving read data back from DDR2. This is
                                                         done to offset any board trace induced delay on
                                                         the DQ and DQS which inherently makes these
                                                         asynchronous with respect to the internal clk of
                                                         controller. TSKW moves this sampling window by
                                                         integer cycles. SILO_QC and HC move this quarter
                                                         and half a cycle respectively. */
	uint64_t sil_lat                      : 2;  /**< Silo Latency (\#fclks): On reads, determines how many
                                                         additional fclks to wait (on top of CASLAT+1) before
                                                         pulling data out of the padring silos used for time
                                                         domain boundary crossing.
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t bprch                        : 1;  /**< Tristate Enable (back porch) (\#fclks)
                                                         On reads, allows user to control the shape of the
                                                         tristate disable back porch for the DQ data bus.
                                                         This parameter is also very dependent on the
                                                         RW_DLY and WR_DLY parameters and care must be
                                                         taken when programming these parameters to avoid
                                                         data bus contention. Valid range [0..2]
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t fprch                        : 1;  /**< Tristate Enable (front porch) (\#fclks)
                                                         On reads, allows user to control the shape of the
                                                         tristate disable front porch for the DQ data bus.
                                                         This parameter is also very dependent on the
                                                         RW_DLY and WR_DLY parameters and care must be
                                                         taken when programming these parameters to avoid
                                                         data bus contention. Valid range [0..2]
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t init                         : 1;  /**< When a '1' is written (and the previous value was '0'),
                                                         the HW init sequence(s) for the LLM Memory Port is
                                                         initiated.
                                                         NOTE: To initialize memory, SW must:
                                                           1) Enable memory port
                                                               a) PRTENA=1
                                                           2) Wait 200us (to ensure a stable clock
                                                              to the DDR2) - as per DDR2 spec.
                                                           3) Write a '1' to the INIT which
                                                              will initiate a hardware initialization
                                                              sequence.
                                                         NOTE: After writing a '1', SW must wait 64K eclk
                                                         cycles to ensure the HW init sequence has completed
                                                         before writing to ANY of the DFA_DDR2* registers.
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t prtena                       : 1;  /**< Enable DFA Memory
                                                         When enabled, this bit lets N3K be the default
                                                         driver for DFA-LLM memory port. */
#else
	uint64_t prtena                       : 1;
	uint64_t init                         : 1;
	uint64_t fprch                        : 1;
	uint64_t bprch                        : 1;
	uint64_t sil_lat                      : 2;
	uint64_t silo_hc                      : 1;
	uint64_t silo_qc                      : 1;
	uint64_t rnk_msk                      : 4;
	uint64_t tskw                         : 2;
	uint64_t reserved_14_15               : 2;
	uint64_t ref_int                      : 13;
	uint64_t reserved_29_31               : 3;
	uint64_t fpip                         : 3;
	uint64_t mrs_pgm                      : 1;
	uint64_t trfc                         : 5;
	uint64_t reserved_41_63               : 23;
#endif
	} s;
	struct cvmx_dfa_ddr2_cfg_s            cn31xx;
};
typedef union cvmx_dfa_ddr2_cfg cvmx_dfa_ddr2_cfg_t;

/**
 * cvmx_dfa_ddr2_comp
 *
 * DFA_DDR2_COMP = DFA DDR2 I/O PVT Compensation Configuration
 *
 *
 * Description: The following are registers to program the DDR2 PLL and DLL
 */
union cvmx_dfa_ddr2_comp {
	uint64_t u64;
	struct cvmx_dfa_ddr2_comp_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t dfa__pctl                    : 4;  /**< DFA DDR pctl from compensation circuit
                                                         Internal DBG only */
	uint64_t dfa__nctl                    : 4;  /**< DFA DDR nctl from compensation circuit
                                                         Internal DBG only */
	uint64_t reserved_9_55                : 47;
	uint64_t pctl_csr                     : 4;  /**< Compensation control bits */
	uint64_t nctl_csr                     : 4;  /**< Compensation control bits */
	uint64_t comp_bypass                  : 1;  /**< Compensation Bypass */
#else
	uint64_t comp_bypass                  : 1;
	uint64_t nctl_csr                     : 4;
	uint64_t pctl_csr                     : 4;
	uint64_t reserved_9_55                : 47;
	uint64_t dfa__nctl                    : 4;
	uint64_t dfa__pctl                    : 4;
#endif
	} s;
	struct cvmx_dfa_ddr2_comp_s           cn31xx;
};
typedef union cvmx_dfa_ddr2_comp cvmx_dfa_ddr2_comp_t;

/**
 * cvmx_dfa_ddr2_emrs
 *
 * DFA_DDR2_EMRS = DDR2 EMRS Register(s) EMRS1[14:0], EMRS1_OCD[14:0]
 * Description: This register contains the data driven onto the Address[14:0] lines during  DDR INIT
 * To support Clamshelling (where N3K DFA_A[] pins are not 1:1 mapped to each clam(or rank), a HW init
 * sequence is allowed on a "per-rank" basis. Care must be taken in the values programmed into these
 * registers during the HW initialization sequence (see N3K specific restrictions in notes below).
 * DFA_DDR2_CFG[MRS_PGM] must be 1 to support this feature.
 *
 * Notes:
 * For DDR-II please consult your device's data sheet for further details:
 *
 */
union cvmx_dfa_ddr2_emrs {
	uint64_t u64;
	struct cvmx_dfa_ddr2_emrs_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_31_63               : 33;
	uint64_t emrs1_ocd                    : 15; /**< Memory Address[14:0] during "EMRS1 (OCD Calibration)"
                                                         step \#12a "EMRS OCD Default Command" A[9:7]=111
                                                         of DDR2 HW initialization sequence.
                                                         (See JEDEC DDR2 specification (JESD79-2):
                                                         Power Up and initialization sequence).
                                                            A[14:13] = 0, RESERVED
                                                            A[12] = 0, Output Buffers Enabled
                                                            A[11] = 0, RDQS Disabled (we do not support RDQS)
                                                            A[10] = 0, DQSn Enabled
                                                            A[9:7] = 7, OCD Calibration Mode Default
                                                            A[6] = 0, ODT Disabled
                                                            A[5:3]=DFA_DDR2_TMG[ADDLAT]  Additive LATENCY (Default 0)
                                                            A[2]=0    Termination Res RTT (ODT off Default)
                                                            [A6,A2] = 0 -> ODT Disabled
                                                                      1 -> 75 ohm; 2 -> 150 ohm; 3 - Reserved
                                                            A[1]=0  Normal Output Driver Imp mode
                                                                    (1 - weak ie., 60% of normal drive strength)
                                                            A[0] = 0 DLL Enabled */
	uint64_t reserved_15_15               : 1;
	uint64_t emrs1                        : 15; /**< Memory Address[14:0] during:
                                                           a) Step \#7 "EMRS1 to enable DLL (A[0]=0)"
                                                           b) Step \#12b "EMRS OCD Calibration Mode Exit"
                                                         steps of DDR2 HW initialization sequence.
                                                         (See JEDEC DDR2 specification (JESD79-2): Power Up and
                                                         initialization sequence).
                                                           A[14:13] = 0, RESERVED
                                                           A[12] = 0, Output Buffers Enabled
                                                           A[11] = 0, RDQS Disabled (we do not support RDQS)
                                                           A[10] = 0, DQSn Enabled
                                                           A[9:7] = 0, OCD Calibration Mode exit/maintain
                                                           A[6] = 0, ODT Disabled
                                                           A[5:3]=DFA_DDR2_TMG[ADDLAT]  Additive LATENCY (Default 0)
                                                           A[2]=0    Termination Res RTT (ODT off Default)
                                                           [A6,A2] = 0 -> ODT Disabled
                                                                     1 -> 75 ohm; 2 -> 150 ohm; 3 - Reserved
                                                           A[1]=0  Normal Output Driver Imp mode
                                                                   (1 - weak ie., 60% of normal drive strength)
                                                           A[0] = 0 DLL Enabled */
#else
	uint64_t emrs1                        : 15;
	uint64_t reserved_15_15               : 1;
	uint64_t emrs1_ocd                    : 15;
	uint64_t reserved_31_63               : 33;
#endif
	} s;
	struct cvmx_dfa_ddr2_emrs_s           cn31xx;
};
typedef union cvmx_dfa_ddr2_emrs cvmx_dfa_ddr2_emrs_t;

/**
 * cvmx_dfa_ddr2_fcnt
 *
 * DFA_DDR2_FCNT = DFA FCLK Counter
 *
 *
 * Description: This FCLK cycle counter gets going after memory has been initialized
 */
union cvmx_dfa_ddr2_fcnt {
	uint64_t u64;
	struct cvmx_dfa_ddr2_fcnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_47_63               : 17;
	uint64_t fcyc_cnt                     : 47; /**< Counter counts FCLK cycles or \# cycles that the memory
                                                         controller has requests queued up depending on FCNT_MODE
                                                         If FCNT_MODE = 0, this counter counts the \# FCLK cycles
                                                         If FCNT_MODE = 1, this counter counts the \# cycles the
                                                         controller is active with memory requests. */
#else
	uint64_t fcyc_cnt                     : 47;
	uint64_t reserved_47_63               : 17;
#endif
	} s;
	struct cvmx_dfa_ddr2_fcnt_s           cn31xx;
};
typedef union cvmx_dfa_ddr2_fcnt cvmx_dfa_ddr2_fcnt_t;

/**
 * cvmx_dfa_ddr2_mrs
 *
 * DFA_DDR2_MRS = DDR2 MRS Register(s) MRS_DLL[14:0], MRS[14:0]
 * Description: This register contains the data driven onto the Address[14:0] lines during DDR INIT
 * To support Clamshelling (where N3K DFA_A[] pins are not 1:1 mapped to each clam(or rank), a HW init
 * sequence is allowed on a "per-rank" basis. Care must be taken in the values programmed into these
 * registers during the HW initialization sequence (see N3K specific restrictions in notes below).
 * DFA_DDR2_CFG[MRS_PGM] must be 1 to support this feature.
 *
 * Notes:
 * For DDR-II please consult your device's data sheet for further details:
 *
 */
union cvmx_dfa_ddr2_mrs {
	uint64_t u64;
	struct cvmx_dfa_ddr2_mrs_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_31_63               : 33;
	uint64_t mrs                          : 15; /**< Memory Address[14:0] during "MRS without resetting
                                                         DLL A[8]=0" step of HW initialization sequence.
                                                         (See JEDEC DDR2 specification (JESD79-2): Power Up
                                                         and initialization sequence - Step \#11).
                                                           A[14:13] = 0, RESERVED
                                                           A[12] = 0, Fast Active Power Down Mode
                                                           A[11:9] = DFA_DDR2_TMG[TWR]
                                                           A[8] = 0, for DLL Reset
                                                           A[7] =0  Test Mode (must be 0 for normal operation)
                                                           A[6:4]=DFA_DDR2_TMG[CASLAT] CAS LATENCY (default 4)
                                                           A[3]=0    Burst Type(must be 0:Sequential)
                                                           A[2:0]=2  Burst Length=4(default) */
	uint64_t reserved_15_15               : 1;
	uint64_t mrs_dll                      : 15; /**< Memory Address[14:0] during "MRS for DLL_RESET A[8]=1"
                                                         step of HW initialization sequence.
                                                         (See JEDEC DDR2 specification (JESD79-2): Power Up
                                                         and initialization sequence - Step \#8).
                                                           A[14:13] = 0, RESERVED
                                                           A[12] = 0, Fast Active Power Down Mode
                                                           A[11:9] = DFA_DDR2_TMG[TWR]
                                                           A[8] = 1, for DLL Reset
                                                           A[7] = 0  Test Mode (must be 0 for normal operation)
                                                           A[6:4]=DFA_DDR2_TMG[CASLAT]    CAS LATENCY (default 4)
                                                           A[3] = 0    Burst Type(must be 0:Sequential)
                                                           A[2:0] = 2  Burst Length=4(default) */
#else
	uint64_t mrs_dll                      : 15;
	uint64_t reserved_15_15               : 1;
	uint64_t mrs                          : 15;
	uint64_t reserved_31_63               : 33;
#endif
	} s;
	struct cvmx_dfa_ddr2_mrs_s            cn31xx;
};
typedef union cvmx_dfa_ddr2_mrs cvmx_dfa_ddr2_mrs_t;

/**
 * cvmx_dfa_ddr2_opt
 *
 * DFA_DDR2_OPT = DFA DDR2 Optimization Registers
 *
 *
 * Description: The following are registers to tweak certain parameters to boost performance
 */
union cvmx_dfa_ddr2_opt {
	uint64_t u64;
	struct cvmx_dfa_ddr2_opt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_10_63               : 54;
	uint64_t max_read_batch               : 5;  /**< Maximum number of consecutive read to service before
                                                         allowing write to interrupt. */
	uint64_t max_write_batch              : 5;  /**< Maximum number of consecutive writes to service before
                                                         allowing reads to interrupt. */
#else
	uint64_t max_write_batch              : 5;
	uint64_t max_read_batch               : 5;
	uint64_t reserved_10_63               : 54;
#endif
	} s;
	struct cvmx_dfa_ddr2_opt_s            cn31xx;
};
typedef union cvmx_dfa_ddr2_opt cvmx_dfa_ddr2_opt_t;

/**
 * cvmx_dfa_ddr2_pll
 *
 * DFA_DDR2_PLL = DFA DDR2 PLL and DLL Configuration
 *
 *
 * Description: The following are registers to program the DDR2 PLL and DLL
 */
union cvmx_dfa_ddr2_pll {
	uint64_t u64;
	struct cvmx_dfa_ddr2_pll_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t pll_setting                  : 17; /**< Internal Debug Use Only */
	uint64_t reserved_32_46               : 15;
	uint64_t setting90                    : 5;  /**< Contains the setting of DDR DLL; Internal DBG only */
	uint64_t reserved_21_26               : 6;
	uint64_t dll_setting                  : 5;  /**< Contains the open loop setting value for the DDR90 delay
                                                         line. */
	uint64_t dll_byp                      : 1;  /**< DLL Bypass. When set, the DDR90 DLL is bypassed and
                                                         the DLL behaves in Open Loop giving a fixed delay
                                                         set by DLL_SETTING */
	uint64_t qdll_ena                     : 1;  /**< DDR Quad DLL Enable: A 0->1 transition on this bit after
                                                         erst deassertion will reset the DDR 90 DLL. Allow
                                                         200 micro seconds for Lock before DDR Init. */
	uint64_t bw_ctl                       : 4;  /**< Internal Use Only - for Debug */
	uint64_t bw_upd                       : 1;  /**< Internal Use Only - for Debug */
	uint64_t pll_div2                     : 1;  /**< PLL Output is further divided by 2. Useful for slow
                                                         fclk frequencies where the PLL may be out of range. */
	uint64_t reserved_7_7                 : 1;
	uint64_t pll_ratio                    : 5;  /**< Bits <6:2> sets the clk multiplication ratio
                                                         If the fclk frequency desired is less than 260MHz
                                                         (lower end saturation point of the pll), write 2x
                                                         the ratio desired in this register and set PLL_DIV2 */
	uint64_t pll_bypass                   : 1;  /**< PLL Bypass. Uses the ref_clk without multiplication. */
	uint64_t pll_init                     : 1;  /**< Need a 0 to 1 pulse on this CSR to get the DFA
                                                         Clk Generator Started. Write this register before
                                                         starting anything. Allow 200 uS for PLL Lock before
                                                         doing anything. */
#else
	uint64_t pll_init                     : 1;
	uint64_t pll_bypass                   : 1;
	uint64_t pll_ratio                    : 5;
	uint64_t reserved_7_7                 : 1;
	uint64_t pll_div2                     : 1;
	uint64_t bw_upd                       : 1;
	uint64_t bw_ctl                       : 4;
	uint64_t qdll_ena                     : 1;
	uint64_t dll_byp                      : 1;
	uint64_t dll_setting                  : 5;
	uint64_t reserved_21_26               : 6;
	uint64_t setting90                    : 5;
	uint64_t reserved_32_46               : 15;
	uint64_t pll_setting                  : 17;
#endif
	} s;
	struct cvmx_dfa_ddr2_pll_s            cn31xx;
};
typedef union cvmx_dfa_ddr2_pll cvmx_dfa_ddr2_pll_t;

/**
 * cvmx_dfa_ddr2_tmg
 *
 * DFA_DDR2_TMG = DFA DDR2 Memory Timing Config Register
 *
 *
 * Description: The following are registers to program the DDR2 memory timing parameters.
 */
union cvmx_dfa_ddr2_tmg {
	uint64_t u64;
	struct cvmx_dfa_ddr2_tmg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_47_63               : 17;
	uint64_t fcnt_mode                    : 1;  /**< If FCNT_MODE = 0, this counter counts the \# FCLK cycles
                                                         If FCNT_MODE = 1, this counter counts the \# cycles the
                                                         controller is active with memory requests. */
	uint64_t cnt_clr                      : 1;  /**< Clears the FCLK Cyc & Bus Util counter */
	uint64_t cavmipo                      : 1;  /**< RESERVED */
	uint64_t ctr_rst                      : 1;  /**< Reset oneshot pulse for refresh counter & Perf counters
                                                         SW should first write this field to a one to clear
                                                         & then write to a zero for normal operation */
	uint64_t odt_rtt                      : 2;  /**< DDR2 Termination Resistor Setting
                                                         These two bits are loaded into the RTT
                                                         portion of the EMRS register bits A6 & A2. If DDR2's
                                                         termination (for the memory's DQ/DQS/DM pads) is not
                                                         desired, set it to 00. If it is, chose between
                                                         01 for 75 ohm and 10 for 150 ohm termination.
                                                              00 = ODT Disabled
                                                              01 = 75 ohm Termination
                                                              10 = 150 ohm Termination
                                                              11 = 50 ohm Termination */
	uint64_t dqsn_ena                     : 1;  /**< For DDR-II Mode, DIC[1] is used to load into EMRS
                                                         bit 10 - DQSN Enable/Disable field. By default, we
                                                         program the DDR's to drive the DQSN also. Set it to
                                                         1 if DQSN should be Hi-Z.
                                                              0 - DQSN Enable
                                                              1 - DQSN Disable */
	uint64_t dic                          : 1;  /**< Drive Strength Control:
                                                         For DDR-I/II Mode, DIC[0] is
                                                         loaded into the Extended Mode Register (EMRS) A1 bit
                                                         during initialization. (see DDR-I data sheet EMRS
                                                         description)
                                                              0 = Normal
                                                              1 = Reduced */
	uint64_t r2r_slot                     : 1;  /**< A 1 on this register will force the controller to
                                                         slot a bubble between every reads */
	uint64_t tfaw                         : 5;  /**< tFAW - Cycles = RNDUP[tFAW(ns)/tcyc(ns)] - 1
                                                         Four Access Window time. Relevant only in
                                                         8-bank parts.
                                                              TFAW = 5'b0 for DDR2-4bank
                                                              TFAW = RNDUP[tFAW(ns)/tcyc(ns)] - 1 in DDR2-8bank */
	uint64_t twtr                         : 4;  /**< tWTR Cycles = RNDUP[tWTR(ns)/tcyc(ns)]
                                                         Last Wr Data to Rd Command time.
                                                         (Represented in fclk cycles)
                                                         TYP=15ns
                                                              - 0000: RESERVED
                                                              - 0001: 1
                                                              - ...
                                                              - 0111: 7
                                                              - 1000-1111: RESERVED */
	uint64_t twr                          : 3;  /**< DDR Write Recovery time (tWR). Last Wr Brst to Prech
                                                         This is not a direct encoding of the value. Its
                                                         programmed as below per DDR2 spec. The decimal number
                                                         on the right is RNDUP(tWR(ns) / clkFreq)
                                                         TYP=15ns
                                                              - 000: RESERVED
                                                              - 001: 2
                                                              - 010: 3
                                                              - 011: 4
                                                              - 100: 5
                                                              - 101: 6
                                                              - 110-111: RESERVED */
	uint64_t trp                          : 4;  /**< tRP Cycles = RNDUP[tRP(ns)/tcyc(ns)]
                                                         (Represented in fclk cycles)
                                                         TYP=15ns
                                                              - 0000: RESERVED
                                                              - 0001: 1
                                                              - ...
                                                              - 0111: 7
                                                              - 1000-1111: RESERVED
                                                         When using parts with 8 banks (DFA_CFG->MAX_BNK
                                                         is 1), load tRP cycles + 1 into this register. */
	uint64_t tras                         : 5;  /**< tRAS Cycles = RNDUP[tRAS(ns)/tcyc(ns)]
                                                         (Represented in fclk cycles)
                                                         TYP=45ns
                                                              - 00000-0001: RESERVED
                                                              - 00010: 2
                                                              - ...
                                                              - 10100: 20
                                                              - 10101-11111: RESERVED */
	uint64_t trrd                         : 3;  /**< tRRD cycles: ACT-ACT timing parameter for different
                                                         banks. (Represented in fclk cycles)
                                                         For DDR2, TYP=7.5ns
                                                             - 000: RESERVED
                                                             - 001: 1 tCYC
                                                             - 010: 2 tCYC
                                                             - 011: 3 tCYC
                                                             - 100: 4 tCYC
                                                             - 101: 5 tCYC
                                                             - 110-111: RESERVED */
	uint64_t trcd                         : 4;  /**< tRCD Cycles = RNDUP[tRCD(ns)/tcyc(ns)]
                                                         (Represented in fclk cycles)
                                                         TYP=15ns
                                                              - 0000: RESERVED
                                                              - 0001: 2 (2 is the smallest value allowed)
                                                              - 0002: 2
                                                              - ...
                                                              - 0111: 7
                                                              - 1110-1111: RESERVED */
	uint64_t addlat                       : 3;  /**< When in Posted CAS mode ADDLAT needs to be programmed
                                                         to tRCD-1
                                                               ADDLAT         \#additional latency cycles
                                                                000              0
                                                                001              1 (tRCD = 2 fclk's)
                                                                010              2 (tRCD = 3 fclk's)
                                                                011              3 (tRCD = 4 fclk's)
                                                                100              4 (tRCD = 5 fclk's)
                                                                101              5 (tRCD = 6 fclk's)
                                                                110              6 (tRCD = 7 fclk's)
                                                                111              7 (tRCD = 8 fclk's) */
	uint64_t pocas                        : 1;  /**< Posted CAS mode. When 1, we use DDR2's Posted CAS
                                                         feature. When using this mode, ADDLAT needs to be
                                                         programmed as well */
	uint64_t caslat                       : 3;  /**< CAS Latency in \# fclk Cycles
                                                         CASLAT           \#  CAS latency cycles
                                                          000 - 010           RESERVED
                                                          011                    3
                                                          100                    4
                                                          101                    5
                                                          110                    6
                                                          111                    7 */
	uint64_t tmrd                         : 2;  /**< tMRD Cycles
                                                         (Represented in fclk tCYC)
                                                         For DDR2, its TYP 2*tCYC)
                                                             - 000: RESERVED
                                                             - 001: 1
                                                             - 010: 2
                                                             - 011: 3 */
	uint64_t ddr2t                        : 1;  /**< When 2T mode is turned on, command signals are
                                                         setup a cycle ahead of when the CS is enabled
                                                         and kept for a total of 2 cycles. This mode is
                                                         enabled in higher speeds when there is difficulty
                                                         meeting setup. Performance could
                                                         be negatively affected in 2T mode */
#else
	uint64_t ddr2t                        : 1;
	uint64_t tmrd                         : 2;
	uint64_t caslat                       : 3;
	uint64_t pocas                        : 1;
	uint64_t addlat                       : 3;
	uint64_t trcd                         : 4;
	uint64_t trrd                         : 3;
	uint64_t tras                         : 5;
	uint64_t trp                          : 4;
	uint64_t twr                          : 3;
	uint64_t twtr                         : 4;
	uint64_t tfaw                         : 5;
	uint64_t r2r_slot                     : 1;
	uint64_t dic                          : 1;
	uint64_t dqsn_ena                     : 1;
	uint64_t odt_rtt                      : 2;
	uint64_t ctr_rst                      : 1;
	uint64_t cavmipo                      : 1;
	uint64_t cnt_clr                      : 1;
	uint64_t fcnt_mode                    : 1;
	uint64_t reserved_47_63               : 17;
#endif
	} s;
	struct cvmx_dfa_ddr2_tmg_s            cn31xx;
};
typedef union cvmx_dfa_ddr2_tmg cvmx_dfa_ddr2_tmg_t;

/**
 * cvmx_dfa_debug0
 *
 * DFA_DEBUG0 = DFA Scoreboard Debug \#0 Register
 * *FOR INTERNAL USE ONLY*
 * Description: When the DFA_CONTROL[SBDLCK] bit is written '1', the contents of this register are locked down.
 * Otherwise, the contents of this register are the 'active' contents of the DFA Scoreboard at the time of the
 * CSR read.
 * VERIFICATION NOTE: Read data is unsafe. X's(undefined data) can propagate (in the behavioral model)
 * on the reads unless the DTE Engine specified by DFA_CONTROL[SBDNUM] has previously been assigned an
 * instruction.
 */
union cvmx_dfa_debug0 {
	uint64_t u64;
	struct cvmx_dfa_debug0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t sbd0                         : 64; /**< DFA ScoreBoard \#0 Data
                                                         (DFA Scoreboard Debug)
                                                            [63:38]   (26) rptr[28:3]: Result Base Pointer (QW-aligned)
                                                            [37:22]   (16) Cumulative Result Write Counter (for HDR write)
                                                            [21]       (1) Waiting for GRdRsp EOT
                                                            [20]       (1) Waiting for GRdReq Issue (to NRQ)
                                                            [19]       (1) GLPTR/GLCNT Valid
                                                            [18]       (1) Completion Mark Detected
                                                            [17:15]    (3) Completion Code [0=PDGONE/1=PERR/2=RFULL/3=TERM]
                                                            [14]       (1) Completion Detected
                                                            [13]       (1) Waiting for HDR RWrCmtRsp
                                                            [12]       (1) Waiting for LAST RESULT RWrCmtRsp
                                                            [11]       (1) Waiting for HDR RWrReq
                                                            [10]        (1) Waiting for RWrReq
                                                            [9]        (1) Waiting for WQWrReq issue
                                                            [8]        (1) Waiting for PRdRsp EOT
                                                            [7]        (1) Waiting for PRdReq Issue (to NRQ)
                                                            [6]        (1) Packet Data Valid
                                                            [5]        (1) WQVLD
                                                            [4]        (1) WQ Done Point (either WQWrReq issued (for WQPTR<>0) OR HDR RWrCmtRsp)
                                                            [3]        (1) Resultant write STF/P Mode
                                                            [2]        (1) Packet Data LDT mode
                                                            [1]        (1) Gather Mode
                                                            [0]        (1) Valid */
#else
	uint64_t sbd0                         : 64;
#endif
	} s;
	struct cvmx_dfa_debug0_s              cn61xx;
	struct cvmx_dfa_debug0_s              cn63xx;
	struct cvmx_dfa_debug0_s              cn63xxp1;
	struct cvmx_dfa_debug0_s              cn66xx;
	struct cvmx_dfa_debug0_s              cn68xx;
	struct cvmx_dfa_debug0_s              cn68xxp1;
};
typedef union cvmx_dfa_debug0 cvmx_dfa_debug0_t;

/**
 * cvmx_dfa_debug1
 *
 * DFA_DEBUG1 = DFA Scoreboard Debug \#1 Register
 * *FOR INTERNAL USE ONLY*
 * Description: When the DFA_CONTROL[SBDLCK] bit is written '1', the contents of this register are locked down.
 * Otherwise, the contents of this register are the 'active' contents of the DFA Scoreboard at the time of the
 * CSR read.
 * VERIFICATION NOTE: Read data is unsafe. X's(undefined data) can propagate (in the behavioral model)
 * on the reads unless the DTE Engine specified by DFA_CONTROL[SBDNUM] has previously been assigned an
 * instruction.
 */
union cvmx_dfa_debug1 {
	uint64_t u64;
	struct cvmx_dfa_debug1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t sbd1                         : 64; /**< DFA ScoreBoard \#1 Data
                                                         DFA Scoreboard Debug Data
                                                            [63:56]   (8) UNUSED
                                                            [55:16]  (40) Packet Data Pointer
                                                            [15:0]   (16) Packet Data Counter */
#else
	uint64_t sbd1                         : 64;
#endif
	} s;
	struct cvmx_dfa_debug1_s              cn61xx;
	struct cvmx_dfa_debug1_s              cn63xx;
	struct cvmx_dfa_debug1_s              cn63xxp1;
	struct cvmx_dfa_debug1_s              cn66xx;
	struct cvmx_dfa_debug1_s              cn68xx;
	struct cvmx_dfa_debug1_s              cn68xxp1;
};
typedef union cvmx_dfa_debug1 cvmx_dfa_debug1_t;

/**
 * cvmx_dfa_debug2
 *
 * DFA_DEBUG2 = DFA Scoreboard Debug \#2 Register
 *
 * Description: When the DFA_CONTROL[SBDLCK] bit is written '1', the contents of this register are locked down.
 * Otherwise, the contents of this register are the 'active' contents of the DFA Scoreboard at the time of the
 * CSR read.
 * VERIFICATION NOTE: Read data is unsafe. X's(undefined data) can propagate (in the behavioral model)
 * on the reads unless the DTE Engine specified by DFA_CONTROL[SBDNUM] has previously been assigned an
 * instruction.
 */
union cvmx_dfa_debug2 {
	uint64_t u64;
	struct cvmx_dfa_debug2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t sbd2                         : 64; /**< DFA ScoreBoard \#2 Data
                                                         [63:45] (19) UNUSED
                                                         [44:42]  (3) Instruction Type
                                                         [41:5]  (37) rwptr[39:3]: Result Write Pointer
                                                         [4:0]    (5) prwcnt[4:0]: Pending Result Write Counter */
#else
	uint64_t sbd2                         : 64;
#endif
	} s;
	struct cvmx_dfa_debug2_s              cn61xx;
	struct cvmx_dfa_debug2_s              cn63xx;
	struct cvmx_dfa_debug2_s              cn63xxp1;
	struct cvmx_dfa_debug2_s              cn66xx;
	struct cvmx_dfa_debug2_s              cn68xx;
	struct cvmx_dfa_debug2_s              cn68xxp1;
};
typedef union cvmx_dfa_debug2 cvmx_dfa_debug2_t;

/**
 * cvmx_dfa_debug3
 *
 * DFA_DEBUG3 = DFA Scoreboard Debug \#3 Register
 *
 * Description: When the DFA_CONTROL[SBDLCK] bit is written '1', the contents of this register are locked down.
 * Otherwise, the contents of this register are the 'active' contents of the DFA Scoreboard at the time of the
 * CSR read.
 * VERIFICATION NOTE: Read data is unsafe. X's(undefined data) can propagate (in the behavioral model)
 * on the reads unless the DTE Engine specified by DFA_CONTROL[SBDNUM] has previously been assigned an
 * instruction.
 */
union cvmx_dfa_debug3 {
	uint64_t u64;
	struct cvmx_dfa_debug3_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t sbd3                         : 64; /**< DFA ScoreBoard \#3 Data
                                                         [63:52] (11) rptr[39:29]: Result Base Pointer (QW-aligned)
                                                         [52:16] (37) glptr[39:3]: Gather List Pointer
                                                         [15:0]  (16) glcnt Gather List Counter */
#else
	uint64_t sbd3                         : 64;
#endif
	} s;
	struct cvmx_dfa_debug3_s              cn61xx;
	struct cvmx_dfa_debug3_s              cn63xx;
	struct cvmx_dfa_debug3_s              cn63xxp1;
	struct cvmx_dfa_debug3_s              cn66xx;
	struct cvmx_dfa_debug3_s              cn68xx;
	struct cvmx_dfa_debug3_s              cn68xxp1;
};
typedef union cvmx_dfa_debug3 cvmx_dfa_debug3_t;

/**
 * cvmx_dfa_difctl
 *
 * DFA_DIFCTL = DFA Instruction FIFO (DIF) Control Register
 *
 * Description:
 *  NOTE: To write to the DFA_DIFCTL register, a device would issue an IOBST directed at the DFA with addr[34:32]=3'b110.
 *        To read the DFA_DIFCTL register, a device would issue an IOBLD64 directed at the DFA with addr[34:32]=3'b110.
 *
 *  NOTE: This register is intended to ONLY be written once (at power-up). Any future writes could
 *  cause the DFA and FPA HW to become unpredictable.
 *
 *  NOTE: If DFA_CONFIG[DTECLKDIS]=1 (DFA-DTE clocks disabled), reads/writes to the DFA_DIFCTL register do not take effect.
 *  NOTE: If FUSE[TBD]="DFA DTE disable" is blown, reads/writes to the DFA_DIFCTL register do not take effect.
 */
union cvmx_dfa_difctl {
	uint64_t u64;
	struct cvmx_dfa_difctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_26_63               : 38;
	uint64_t msegbase                     : 6;  /**< Memory Segmentation Base Address
                                                         For debug purposes, backdoor accesses to the DFA
                                                         memory are supported via NCB-Direct CSR accesses to
                                                         the DFA Memory REGION(if addr[34:32]=5. However due
                                                         to the existing NCB address decoding scheme, the
                                                         address only offers a 4GB extent into the DFA memory
                                                         REGION. Therefore, the MSEGBASE CSR field provides
                                                         the additional upper memory address bits to allow access
                                                         to the full extent of memory (128GB MAX).
                                                         For DFA Memory REGION read NCB-Direct CSR accesses, the
                                                         38bit L2/DRAM memory byte address is generated as follows:
                                                           memaddr[37:0] = [DFA_DIFCTL[MSEGBASE],ncb_addr[31:3],3'b0]
                                                         NOTE: See the upper 6bits of the memory address are sourced
                                                         from DFA_DIFCTL[MSEGBASE] CSR field. The lower 4GB address
                                                         offset is directly referenced using the NCB address bits during
                                                         the reference itself.
                                                         NOTE: The DFA_DIFCTL[MSEGBASE] is shared amongst all references.
                                                         As such, if multiple PPs are accessing different segments in memory,
                                                         their must be a SW mutual exclusive lock during each DFA Memory
                                                         REGION access to avoid collisions between PPs using the same MSEGBASE
                                                         CSR field.
                                                         NOTE: See also DFA_ERROR[DFANXM] programmable interrupt which is
                                                         flagged if SW tries to access non-existent memory space (address hole
                                                         or upper unused region of 38bit address space). */
	uint64_t dwbcnt                       : 8;  /**< Represents the \# of cache lines in the instruction
                                                         buffer that may be dirty and should not be
                                                         written-back to memory when the instruction
                                                         chunk is returned to the Free Page list.
                                                         NOTE: Typically SW will want to mark all DFA
                                                         Instruction memory returned to the Free Page list
                                                         as DWB (Don't WriteBack), therefore SW should
                                                         seed this register as:
                                                           DFA_DIFCTL[DWBCNT] = (DFA_DIFCTL[SIZE] + 4)/4 */
	uint64_t pool                         : 3;  /**< Represents the 3bit buffer pool-id  used by DFA HW
                                                         when the DFA instruction chunk is recycled back
                                                         to the Free Page List maintained by the FPA HW
                                                         (once the DFA instruction has been issued). */
	uint64_t size                         : 9;  /**< Represents the \# of 32B instructions contained
                                                         within each DFA instruction chunk. At Power-on,
                                                         SW will seed the SIZE register with a fixed
                                                         chunk-size. (Must be at least 3)
                                                         DFA HW uses this field to determine the size
                                                         of each DFA instruction chunk, in order to:
                                                            a) determine when to read the next DFA
                                                               instruction chunk pointer which is
                                                               written by SW at the end of the current
                                                               DFA instruction chunk (see DFA description
                                                               of next chunk buffer Ptr for format).
                                                            b) determine when a DFA instruction chunk
                                                               can be returned to the Free Page List
                                                               maintained by the FPA HW. */
#else
	uint64_t size                         : 9;
	uint64_t pool                         : 3;
	uint64_t dwbcnt                       : 8;
	uint64_t msegbase                     : 6;
	uint64_t reserved_26_63               : 38;
#endif
	} s;
	struct cvmx_dfa_difctl_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t dwbcnt                       : 8;  /**< Represents the \# of cache lines in the instruction
                                                         buffer that may be dirty and should not be
                                                         written-back to memory when the instruction
                                                         chunk is returned to the Free Page list.
                                                         NOTE: Typically SW will want to mark all DFA
                                                         Instruction memory returned to the Free Page list
                                                         as DWB (Don't WriteBack), therefore SW should
                                                         seed this register as:
                                                           DFA_DIFCTL[DWBCNT] = (DFA_DIFCTL[SIZE] + 4)/4 */
	uint64_t pool                         : 3;  /**< Represents the 3bit buffer pool-id  used by DFA HW
                                                         when the DFA instruction chunk is recycled back
                                                         to the Free Page List maintained by the FPA HW
                                                         (once the DFA instruction has been issued). */
	uint64_t size                         : 9;  /**< Represents the \# of 32B instructions contained
                                                         within each DFA instruction chunk. At Power-on,
                                                         SW will seed the SIZE register with a fixed
                                                         chunk-size. (Must be at least 3)
                                                         DFA HW uses this field to determine the size
                                                         of each DFA instruction chunk, in order to:
                                                            a) determine when to read the next DFA
                                                               instruction chunk pointer which is
                                                               written by SW at the end of the current
                                                               DFA instruction chunk (see DFA description
                                                               of next chunk buffer Ptr for format).
                                                            b) determine when a DFA instruction chunk
                                                               can be returned to the Free Page List
                                                               maintained by the FPA HW. */
#else
	uint64_t size                         : 9;
	uint64_t pool                         : 3;
	uint64_t dwbcnt                       : 8;
	uint64_t reserved_20_63               : 44;
#endif
	} cn31xx;
	struct cvmx_dfa_difctl_cn31xx         cn38xx;
	struct cvmx_dfa_difctl_cn31xx         cn38xxp2;
	struct cvmx_dfa_difctl_cn31xx         cn58xx;
	struct cvmx_dfa_difctl_cn31xx         cn58xxp1;
	struct cvmx_dfa_difctl_s              cn61xx;
	struct cvmx_dfa_difctl_cn31xx         cn63xx;
	struct cvmx_dfa_difctl_cn31xx         cn63xxp1;
	struct cvmx_dfa_difctl_cn31xx         cn66xx;
	struct cvmx_dfa_difctl_s              cn68xx;
	struct cvmx_dfa_difctl_s              cn68xxp1;
};
typedef union cvmx_dfa_difctl cvmx_dfa_difctl_t;

/**
 * cvmx_dfa_difrdptr
 *
 * DFA_DIFRDPTR = DFA Instruction FIFO (DIF) RDPTR Register
 *
 * Description:
 *  NOTE: To write to the DFA_DIFRDPTR register, a device would issue an IOBST directed at the DFA with addr[34:33]=2'b01.
 *        To read the DFA_DIFRDPTR register, a device would issue an IOBLD64 directed at the DFA with addr[34:33]=2'b01.
 *
 *  NOTE: If DFA_CONFIG[DTECLKDIS]=1 (DFA-DTE clocks disabled), reads/writes to the DFA_DIFRDPTR register do not take effect.
 *  NOTE: If FUSE[TBD]="DFA DTE disable" is blown, reads/writes to the DFA_DIFRDPTR register do not take effect.
 */
union cvmx_dfa_difrdptr {
	uint64_t u64;
	struct cvmx_dfa_difrdptr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_40_63               : 24;
	uint64_t rdptr                        : 35; /**< Represents the 32B-aligned address of the current
                                                         instruction in the DFA Instruction FIFO in main
                                                         memory. The RDPTR must be seeded by software at
                                                         boot time, and is then maintained thereafter
                                                         by DFA HW.
                                                         During the seed write (by SW), RDPTR[6:5]=0,
                                                         since DFA instruction chunks must be 128B aligned.
                                                         During a read (by SW), the 'most recent' contents
                                                         of the RDPTR register are returned at the time
                                                         the NCB-INB bus is driven.
                                                         NOTE: Since DFA HW updates this register, its
                                                         contents are unpredictable in SW (unless
                                                         its guaranteed that no new DoorBell register
                                                         writes have occurred and the DoorBell register is
                                                         read as zero). */
	uint64_t reserved_0_4                 : 5;
#else
	uint64_t reserved_0_4                 : 5;
	uint64_t rdptr                        : 35;
	uint64_t reserved_40_63               : 24;
#endif
	} s;
	struct cvmx_dfa_difrdptr_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t rdptr                        : 31; /**< Represents the 32B-aligned address of the current
                                                         instruction in the DFA Instruction FIFO in main
                                                         memory. The RDPTR must be seeded by software at
                                                         boot time, and is then maintained thereafter
                                                         by DFA HW.
                                                         During the seed write (by SW), RDPTR[6:5]=0,
                                                         since DFA instruction chunks must be 128B aligned.
                                                         During a read (by SW), the 'most recent' contents
                                                         of the RDPTR register are returned at the time
                                                         the NCB-INB bus is driven.
                                                         NOTE: Since DFA HW updates this register, its
                                                         contents are unpredictable in SW (unless
                                                         its guaranteed that no new DoorBell register
                                                         writes have occurred and the DoorBell register is
                                                         read as zero). */
	uint64_t reserved_0_4                 : 5;
#else
	uint64_t reserved_0_4                 : 5;
	uint64_t rdptr                        : 31;
	uint64_t reserved_36_63               : 28;
#endif
	} cn31xx;
	struct cvmx_dfa_difrdptr_cn31xx       cn38xx;
	struct cvmx_dfa_difrdptr_cn31xx       cn38xxp2;
	struct cvmx_dfa_difrdptr_cn31xx       cn58xx;
	struct cvmx_dfa_difrdptr_cn31xx       cn58xxp1;
	struct cvmx_dfa_difrdptr_s            cn61xx;
	struct cvmx_dfa_difrdptr_s            cn63xx;
	struct cvmx_dfa_difrdptr_s            cn63xxp1;
	struct cvmx_dfa_difrdptr_s            cn66xx;
	struct cvmx_dfa_difrdptr_s            cn68xx;
	struct cvmx_dfa_difrdptr_s            cn68xxp1;
};
typedef union cvmx_dfa_difrdptr cvmx_dfa_difrdptr_t;

/**
 * cvmx_dfa_dtcfadr
 *
 * DFA_DTCFADR = DFA DTC Failing Address Register
 *
 * Description: DFA Node Cache Failing Address/Control Error Capture information
 * This register contains useful information to help in isolating a Node Cache RAM failure.
 * NOTE: The first detected PERR failure is captured in DFA_DTCFADR (locked down), until the
 * corresponding PERR Interrupt is cleared by writing one (W1C). (see: DFA_ERR[DC0PERR[2:0]]).
 * NOTE: In the rare event that multiple parity errors are detected in the same cycle from multiple
 * clusters, the FADR register will be locked down for the least signicant cluster \# (0->3).
 */
union cvmx_dfa_dtcfadr {
	uint64_t u64;
	struct cvmx_dfa_dtcfadr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_44_63               : 20;
	uint64_t ram3fadr                     : 12; /**< DFA RAM3 Failing Address
                                                         If DFA_ERR[DC0PERR<2>]=1, this field indicates the
                                                         failing RAM3 Address. The failing address is locked
                                                         down until the DC0PERR<2> W1C occurs.
                                                         NOTE: If multiple DC0PERR<0>=1 errors are detected,
                                                         then the lsb cluster error information is captured. */
	uint64_t reserved_25_31               : 7;
	uint64_t ram2fadr                     : 9;  /**< DFA RAM2 Failing Address
                                                         If DFA_ERR[DC0PERR<1>]=1, this field indicates the
                                                         failing RAM2 Address. The failing address is locked
                                                         down until the DC0PERR<1> W1C occurs.
                                                         NOTE: If multiple DC0PERR<0>=1 errors are detected,
                                                         then the lsb cluster error information is captured. */
	uint64_t reserved_14_15               : 2;
	uint64_t ram1fadr                     : 14; /**< DFA RAM1 Failing Address
                                                         If DFA_ERR[DC0PERR<0>]=1, this field indicates the
                                                         failing RAM1 Address. The failing address is locked
                                                         down until the DC0PERR<0> W1C occurs.
                                                         NOTE: If multiple DC0PERR<0>=1 errors are detected,
                                                         then the lsb cluster error information is captured. */
#else
	uint64_t ram1fadr                     : 14;
	uint64_t reserved_14_15               : 2;
	uint64_t ram2fadr                     : 9;
	uint64_t reserved_25_31               : 7;
	uint64_t ram3fadr                     : 12;
	uint64_t reserved_44_63               : 20;
#endif
	} s;
	struct cvmx_dfa_dtcfadr_s             cn61xx;
	struct cvmx_dfa_dtcfadr_s             cn63xx;
	struct cvmx_dfa_dtcfadr_s             cn63xxp1;
	struct cvmx_dfa_dtcfadr_s             cn66xx;
	struct cvmx_dfa_dtcfadr_s             cn68xx;
	struct cvmx_dfa_dtcfadr_s             cn68xxp1;
};
typedef union cvmx_dfa_dtcfadr cvmx_dfa_dtcfadr_t;

/**
 * cvmx_dfa_eclkcfg
 *
 * Specify the RSL base addresses for the block
 *
 *                  DFA_ECLKCFG = DFA eclk-domain Configuration Registers
 *
 * Description:
 */
union cvmx_dfa_eclkcfg {
	uint64_t u64;
	struct cvmx_dfa_eclkcfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_19_63               : 45;
	uint64_t sbdnum                       : 3;  /**< SBD Debug Entry#
                                                         For internal use only. (DFA Scoreboard debug)
                                                         Selects which one of 8 DFA Scoreboard entries is
                                                         latched into the DFA_SBD_DBG[0-3] registers. */
	uint64_t reserved_15_15               : 1;
	uint64_t sbdlck                       : 1;  /**< DFA Scoreboard LOCK Strobe
                                                         For internal use only. (DFA Scoreboard debug)
                                                         When written with a '1', the DFA Scoreboard Debug
                                                         registers (DFA_SBD_DBG[0-3]) are all locked down.
                                                         This allows SW to lock down the contents of the entire
                                                         SBD for a single instant in time. All subsequent reads
                                                         of the DFA scoreboard registers will return the data
                                                         from that instant in time. */
	uint64_t dcmode                       : 1;  /**< DRF-CRQ/DTE Arbiter Mode
                                                         DTE-DRF Arbiter (0=FP [LP=CRQ/HP=DTE],1=RR)
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t dtmode                       : 1;  /**< DRF-DTE Arbiter Mode
                                                         DTE-DRF Arbiter (0=FP [LP=DTE[15],...,HP=DTE[0]],1=RR)
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t pmode                        : 1;  /**< NCB-NRP Arbiter Mode
                                                         (0=Fixed Priority [LP=WQF,DFF,HP=RGF]/1=RR
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t qmode                        : 1;  /**< NCB-NRQ Arbiter Mode
                                                         (0=Fixed Priority [LP=IRF,RWF,PRF,HP=GRF]/1=RR
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t imode                        : 1;  /**< NCB-Inbound Arbiter
                                                         (0=FP [LP=NRQ,HP=NRP], 1=RR)
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t sarb                         : 1;  /**< DFA Source Arbiter Mode
                                                         Selects the arbitration mode used to select DFA requests
                                                         issued from either CP2 or the DTE (NCB-CSR or DFA HW engine).
                                                          - 0: Fixed Priority [Highest=CP2, Lowest=DTE]
                                                          - 1: Round-Robin
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t reserved_3_7                 : 5;
	uint64_t dteclkdis                    : 1;  /**< DFA DTE Clock Disable
                                                         When SET, the DFA clocks for DTE(thread engine)
                                                         operation are disabled.
                                                         NOTE: When SET, SW MUST NEVER issue ANY operations to
                                                         the DFA via the NCB Bus. All DFA Operations must be
                                                         issued solely through the CP2 interface. */
	uint64_t maxbnk                       : 1;  /**< Maximum Banks per-device (used by the address mapper
                                                         when extracting address bits for the memory bank#.
                                                                 - 0: 4 banks/device
                                                                 - 1: 8 banks/device */
	uint64_t dfa_frstn                    : 1;  /**< Hold this 0 until the DFA DDR PLL and DLL lock
                                                         and then write a 1. A 1 on this register deasserts
                                                         the internal frst_n. Refer to DFA_DDR2_PLL registers for more
                                                         startup information.
                                                         Startup sequence if DFA interface needs to be ON:
                                                          After valid power up,
                                                          Write DFA_DDR2_PLL-> PLL_RATIO & PLL_DIV2 & PLL_BYPASS
                                                          to the appropriate values
                                                          Wait a few cycles
                                                          Write a 1 DFA_DDR2_PLL -> PLL_INIT
                                                          Wait 100 microseconds
                                                          Write a 1 to DFA_DDR2_PLL -> QDLL_ENA
                                                          Wait 10 microseconds
                                                          Write a 1 to this register DFA_FRSTN to pull DFA out of
                                                          reset
                                                          Now the DFA block is ready to be initialized (follow the
                                                          DDR init sequence). */
#else
	uint64_t dfa_frstn                    : 1;
	uint64_t maxbnk                       : 1;
	uint64_t dteclkdis                    : 1;
	uint64_t reserved_3_7                 : 5;
	uint64_t sarb                         : 1;
	uint64_t imode                        : 1;
	uint64_t qmode                        : 1;
	uint64_t pmode                        : 1;
	uint64_t dtmode                       : 1;
	uint64_t dcmode                       : 1;
	uint64_t sbdlck                       : 1;
	uint64_t reserved_15_15               : 1;
	uint64_t sbdnum                       : 3;
	uint64_t reserved_19_63               : 45;
#endif
	} s;
	struct cvmx_dfa_eclkcfg_s             cn31xx;
};
typedef union cvmx_dfa_eclkcfg cvmx_dfa_eclkcfg_t;

/**
 * cvmx_dfa_err
 *
 * DFA_ERR = DFA ERROR Register
 *
 * Description:
 */
union cvmx_dfa_err {
	uint64_t u64;
	struct cvmx_dfa_err_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_33_63               : 31;
	uint64_t dblina                       : 1;  /**< Doorbell Overflow Interrupt Enable bit.
                                                         When set, doorbell overflow conditions are reported. */
	uint64_t dblovf                       : 1;  /**< Doorbell Overflow detected - Status bit
                                                         When set, the 20b accumulated doorbell register
                                                         had overflowed (SW wrote too many doorbell requests).
                                                         If the DBLINA had previously been enabled(set),
                                                         an interrupt will be posted. Software can clear
                                                         the interrupt by writing a 1 to this register bit.
                                                         NOTE: Detection of a Doorbell Register overflow
                                                         is a catastrophic error which may leave the DFA
                                                         HW in an unrecoverable state. */
	uint64_t cp2pina                      : 1;  /**< CP2 LW Mode Parity Error Interrupt Enable bit.
                                                         When set, all PP-generated LW Mode read
                                                         transactions which encounter a parity error (across
                                                         the 36b of data) are reported. */
	uint64_t cp2perr                      : 1;  /**< PP-CP2 Parity Error Detected - Status bit
                                                         When set, a parity error had been detected for a
                                                         PP-generated LW Mode read transaction.
                                                         If the CP2PINA had previously been enabled(set),
                                                         an interrupt will be posted. Software can clear
                                                         the interrupt by writing a 1 to this register bit.
                                                         See also: DFA_MEMFADR CSR which contains more data
                                                         about the memory address/control to help isolate
                                                         the failure. */
	uint64_t cp2parena                    : 1;  /**< CP2 LW Mode Parity Error Enable
                                                         When set, all PP-generated LW Mode read
                                                         transactions which encounter a parity error (across
                                                         the 36b of data) are reported.
                                                         NOTE: This signal must only be written to a different
                                                         value when there are no PP-CP2 transactions
                                                         (preferrably during power-on software initialization). */
	uint64_t dtepina                      : 1;  /**< DTE Parity Error Interrupt Enable bit
                                                         (for 18b SIMPLE mode ONLY).
                                                         When set, all DTE-generated 18b SIMPLE Mode read
                                                         transactions which encounter a parity error (across
                                                         the 17b of data) are reported. */
	uint64_t dteperr                      : 1;  /**< DTE Parity Error Detected (for 18b SIMPLE mode ONLY)
                                                         When set, all DTE-generated 18b SIMPLE Mode read
                                                         transactions which encounter a parity error (across
                                                         the 17b of data) are reported. */
	uint64_t dteparena                    : 1;  /**< DTE Parity Error Enable (for 18b SIMPLE mode ONLY)
                                                         When set, all DTE-generated 18b SIMPLE Mode read
                                                         transactions which encounter a parity error (across
                                                         the 17b of data) are reported.
                                                         NOTE: This signal must only be written to a different
                                                         value when there are no DFA thread engines active
                                                         (preferrably during power-on). */
	uint64_t dtesyn                       : 7;  /**< DTE 29b ECC Failing 6bit Syndrome
                                                         When DTESBE or DTEDBE are set, this field contains
                                                         the failing 7b ECC syndrome. */
	uint64_t dtedbina                     : 1;  /**< DTE 29b Double Bit Error Interrupt Enable bit
                                                         When set, an interrupt is posted for any DTE-generated
                                                         36b SIMPLE Mode read which encounters a double bit
                                                         error. */
	uint64_t dtesbina                     : 1;  /**< DTE 29b Single Bit Error Interrupt Enable bit
                                                         When set, an interrupt is posted for any DTE-generated
                                                         36b SIMPLE Mode read which encounters a single bit
                                                         error (which is also corrected). */
	uint64_t dtedbe                       : 1;  /**< DTE 29b Double Bit Error Detected - Status bit
                                                         When set, a double bit error had been detected
                                                         for a DTE-generated 36b SIMPLE Mode read transaction.
                                                         The DTESYN contains the failing syndrome.
                                                         If the DTEDBINA had previously been enabled(set),
                                                         an interrupt will be posted. Software can clear
                                                         the interrupt by writing a 1 to this register bit.
                                                         See also: DFA_MEMFADR CSR which contains more data
                                                         about the memory address/control to help isolate
                                                         the failure.
                                                         NOTE: DTE-generated 18b SIMPLE Mode Read transactions
                                                         do not participate in ECC check/correct). */
	uint64_t dtesbe                       : 1;  /**< DTE 29b Single Bit Error Corrected - Status bit
                                                         When set, a single bit error had been detected and
                                                         corrected for a DTE-generated 36b SIMPLE Mode read
                                                         transaction.
                                                         If the DTEDBE=0, then the DTESYN contains the
                                                         failing syndrome (used during correction).
                                                         NOTE: DTE-generated 18b SIMPLE Mode Read
                                                         transactions do not participate in ECC check/correct).
                                                         If the DTESBINA had previously been enabled(set),
                                                         an interrupt will be posted. Software can clear
                                                         the interrupt by writing a 1 to this register bit.
                                                         See also: DFA_MEMFADR CSR which contains more data
                                                         about the memory address/control to help isolate
                                                         the failure. */
	uint64_t dteeccena                    : 1;  /**< DTE 29b ECC Enable (for 36b SIMPLE mode ONLY)
                                                         When set, 29b ECC is enabled on all DTE-generated
                                                         36b SIMPLE Mode read transactions.
                                                         NOTE: This signal must only be written to a different
                                                         value when there are no DFA thread engines active
                                                         (preferrably during power-on software initialization). */
	uint64_t cp2syn                       : 8;  /**< PP-CP2 QW ECC Failing 8bit Syndrome
                                                         When CP2SBE or CP2DBE are set, this field contains
                                                         the failing ECC 8b syndrome.
                                                         Refer to CP2ECCENA. */
	uint64_t cp2dbina                     : 1;  /**< PP-CP2 Double Bit Error Interrupt Enable bit
                                                         When set, an interrupt is posted for any PP-generated
                                                         QW Mode read which encounters a double bit error.
                                                         Refer to CP2DBE. */
	uint64_t cp2sbina                     : 1;  /**< PP-CP2 Single Bit Error Interrupt Enable bit
                                                         When set, an interrupt is posted for any PP-generated
                                                         QW Mode read which encounters a single bit error
                                                         (which is also corrected).
                                                         Refer to CP2SBE. */
	uint64_t cp2dbe                       : 1;  /**< PP-CP2 Double Bit Error Detected - Status bit
                                                         When set, a double bit error had been detected
                                                         for a PP-generated QW Mode read transaction.
                                                         The CP2SYN contains the failing syndrome.
                                                          NOTE: PP-generated LW Mode Read transactions
                                                         do not participate in ECC check/correct).
                                                         Refer to CP2ECCENA.
                                                         If the CP2DBINA had previously been enabled(set),
                                                         an interrupt will be posted. Software can clear
                                                         the interrupt by writing a 1 to this register bit.
                                                         See also: DFA_MEMFADR CSR which contains more data
                                                         about the memory address/control to help isolate
                                                         the failure. */
	uint64_t cp2sbe                       : 1;  /**< PP-CP2 Single Bit Error Corrected - Status bit
                                                         When set, a single bit error had been detected and
                                                         corrected for a PP-generated QW Mode read
                                                         transaction.
                                                         If the CP2DBE=0, then the CP2SYN contains the
                                                         failing syndrome (used during correction).
                                                         Refer to CP2ECCENA.
                                                         If the CP2SBINA had previously been enabled(set),
                                                         an interrupt will be posted. Software can clear
                                                         the interrupt by writing a 1 to this register bit.
                                                         See also: DFA_MEMFADR CSR which contains more data
                                                         about the memory address/control to help isolate
                                                         the failure.
                                                         NOTE: PP-generated LW Mode Read transactions
                                                         do not participate in ECC check/correct). */
	uint64_t cp2eccena                    : 1;  /**< PP-CP2 QW ECC Enable (for QW Mode transactions)
                                                         When set, 8bit QW ECC is enabled on all PP-generated
                                                         QW Mode read transactions, CP2SBE and
                                                         CP2DBE may be set, and CP2SYN may be filled.
                                                         NOTE: This signal must only be written to a different
                                                         value when there are no PP-CP2 transactions
                                                         (preferrably during power-on software initialization).
                                                         NOTE: QW refers to a 64-bit LLM Load/Store (intiated
                                                         by a processor core). LW refers to a 36-bit load/store. */
#else
	uint64_t cp2eccena                    : 1;
	uint64_t cp2sbe                       : 1;
	uint64_t cp2dbe                       : 1;
	uint64_t cp2sbina                     : 1;
	uint64_t cp2dbina                     : 1;
	uint64_t cp2syn                       : 8;
	uint64_t dteeccena                    : 1;
	uint64_t dtesbe                       : 1;
	uint64_t dtedbe                       : 1;
	uint64_t dtesbina                     : 1;
	uint64_t dtedbina                     : 1;
	uint64_t dtesyn                       : 7;
	uint64_t dteparena                    : 1;
	uint64_t dteperr                      : 1;
	uint64_t dtepina                      : 1;
	uint64_t cp2parena                    : 1;
	uint64_t cp2perr                      : 1;
	uint64_t cp2pina                      : 1;
	uint64_t dblovf                       : 1;
	uint64_t dblina                       : 1;
	uint64_t reserved_33_63               : 31;
#endif
	} s;
	struct cvmx_dfa_err_s                 cn31xx;
	struct cvmx_dfa_err_s                 cn38xx;
	struct cvmx_dfa_err_s                 cn38xxp2;
	struct cvmx_dfa_err_s                 cn58xx;
	struct cvmx_dfa_err_s                 cn58xxp1;
};
typedef union cvmx_dfa_err cvmx_dfa_err_t;

/**
 * cvmx_dfa_error
 *
 * DFA_ERROR = DFA ERROR Register
 *
 * Description:
 */
union cvmx_dfa_error {
	uint64_t u64;
	struct cvmx_dfa_error_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_19_63               : 45;
	uint64_t replerr                      : 1;  /**< DFA Illegal Replication Factor Error
                                                         For o68: DFA only supports 1x, 2x, and 4x port replication.
                                                         Legal configurations for memory are to support 2 port or
                                                         4 port configurations.
                                                         The REPLERR interrupt will be set in the following illegal
                                                         configuration cases:
                                                             1) An 8x replication factor is detected for any memory reference.
                                                             2) A 4x replication factor is detected for any memory reference
                                                                when only 2 memory ports are enabled.
                                                         NOTE: If REPLERR is set during a DFA Graph Walk operation,
                                                         then the walk will prematurely terminate with RWORD0[REA]=ERR.
                                                         If REPLERR is set during a NCB-Direct CSR read access to DFA
                                                         Memory REGION, then the CSR read response data is UNPREDICTABLE. */
	uint64_t dfanxm                       : 1;  /**< DFA Non-existent Memory Access
                                                         For o68: DTEs (and backdoor CSR DFA Memory REGION reads)
                                                         have access to the following 38bit L2/DRAM address space
                                                         which maps to a 37bit physical DDR3 SDRAM address space.
                                                         see:
                                                         DR0: 0x0 0000 0000 0000 to 0x0 0000 0FFF FFFF
                                                                 maps to lower 256MB of physical DDR3 SDRAM
                                                         DR1: 0x0 0000 2000 0000 to 0x0 0020 0FFF FFFF
                                                                 maps to upper 127.75GB of DDR3 SDRAM
                                                                    L2/DRAM address space                     Physical DDR3 SDRAM Address space
                                                                      (38bit address)                           (37bit address)
                                                                       +-----------+ 0x0020.0FFF.FFFF

                                                                      ===   DR1   ===                          +-----------+ 0x001F.FFFF.FFFF
                                                          (128GB-256MB)|           |
                                                                       |           |                     =>    |           |  (128GB-256MB)
                                                                       +-----------+ 0x0000.1FFF.FFFF          |   DR1
                                                               256MB   |   HOLE    |   (DO NOT USE)
                                                                       +-----------+ 0x0000.0FFF.FFFF          +-----------+ 0x0000.0FFF.FFFF
                                                               256MB   |    DR0    |                           |   DR0     |   (256MB)
                                                                       +-----------+ 0x0000.0000.0000          +-----------+ 0x0000.0000.0000
                                                         In the event the DFA generates a reference to the L2/DRAM
                                                         address hole (0x0000.0FFF.FFFF - 0x0000.1FFF.FFFF) or to
                                                         an address above 0x0020.0FFF.FFFF, the DFANXM programmable
                                                         interrupt bit will be set.
                                                         SWNOTE: Both the 1) SW DFA Graph compiler and the 2) SW NCB-Direct CSR
                                                         accesses to DFA Memory REGION MUST avoid making references
                                                         to these non-existent memory regions.
                                                         NOTE: If DFANXM is set during a DFA Graph Walk operation,
                                                         then the walk will prematurely terminate with RWORD0[REA]=ERR.
                                                         If DFANXM is set during a NCB-Direct CSR read access to DFA
                                                         Memory REGION, then the CSR read response data is forced to
                                                         128'hBADE_FEED_DEAD_BEEF_FACE_CAFE_BEAD_C0DE. (NOTE: the QW
                                                         being accessed, either the upper or lower QW will be returned). */
	uint64_t cndrd                        : 1;  /**< If Any of the cluster's detected a Parity error on RAM1
                                                         this additional bit further specifies that the
                                                         RAM1 parity error was detected during a CND-RD
                                                         (Cache Node Metadata Read).

                                                         For CNDRD Parity Error, the previous CNA arc fetch
                                                         information is written to RWORD1+ as follows:
                                                            RWORD1+[NTYPE]=MNODE
                                                            RWORD1+[NDNID]=cna.ndnid
                                                            RWORD1+[NHMSK]=cna.hmsk
                                                            RWORD1+[NNPTR]=cna.nnptr[13:0]
                                                         NOTE: This bit is set if ANY node cluster's RAM1 accesses
                                                         detect a CNDRD error. */
	uint64_t reserved_15_15               : 1;
	uint64_t dlc1_ovferr                  : 1;  /**< DLC1 Fifo Overflow Error Detected
                                                         This condition should NEVER architecturally occur, and
                                                         is here in case HW credit/debit scheme is not working. */
	uint64_t dlc0_ovferr                  : 1;  /**< DLC0 Fifo Overflow Error Detected
                                                         This condition should NEVER architecturally occur, and
                                                         is here in case HW credit/debit scheme is not working. */
	uint64_t reserved_10_12               : 3;
	uint64_t dc2perr                      : 3;  /**< Cluster#2 RAM[3:1] Parity Error Detected
                                                         See also DFA_DTCFADR register which contains the
                                                         failing addresses for the internal node cache RAMs. */
	uint64_t dc1perr                      : 3;  /**< Cluster#1 RAM[3:1] Parity Error Detected
                                                         See also DFA_DTCFADR register which contains the
                                                         failing addresses for the internal node cache RAMs. */
	uint64_t dc0perr                      : 3;  /**< Cluster#0 RAM[3:1] Parity Error Detected
                                                         See also DFA_DTCFADR register which contains the
                                                         failing addresses for the internal node cache RAMs. */
	uint64_t dblovf                       : 1;  /**< Doorbell Overflow detected - Status bit
                                                         When set, the 20b accumulated doorbell register
                                                         had overflowed (SW wrote too many doorbell requests).
                                                         If the DBLINA had previously been enabled(set),
                                                         an interrupt will be posted. Software can clear
                                                         the interrupt by writing a 1 to this register bit.
                                                         NOTE: Detection of a Doorbell Register overflow
                                                         is a catastrophic error which may leave the DFA
                                                         HW in an unrecoverable state. */
#else
	uint64_t dblovf                       : 1;
	uint64_t dc0perr                      : 3;
	uint64_t dc1perr                      : 3;
	uint64_t dc2perr                      : 3;
	uint64_t reserved_10_12               : 3;
	uint64_t dlc0_ovferr                  : 1;
	uint64_t dlc1_ovferr                  : 1;
	uint64_t reserved_15_15               : 1;
	uint64_t cndrd                        : 1;
	uint64_t dfanxm                       : 1;
	uint64_t replerr                      : 1;
	uint64_t reserved_19_63               : 45;
#endif
	} s;
	struct cvmx_dfa_error_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_19_63               : 45;
	uint64_t replerr                      : 1;  /**< DFA Illegal Replication Factor Error
                                                         For o68: DFA only supports 1x, 2x, and 4x port replication.
                                                         Legal configurations for memory are to support 2 port or
                                                         4 port configurations.
                                                         The REPLERR interrupt will be set in the following illegal
                                                         configuration cases:
                                                             1) An 8x replication factor is detected for any memory reference.
                                                             2) A 4x replication factor is detected for any memory reference
                                                                when only 2 memory ports are enabled.
                                                         NOTE: If REPLERR is set during a DFA Graph Walk operation,
                                                         then the walk will prematurely terminate with RWORD0[REA]=ERR.
                                                         If REPLERR is set during a NCB-Direct CSR read access to DFA
                                                         Memory REGION, then the CSR read response data is UNPREDICTABLE. */
	uint64_t dfanxm                       : 1;  /**< DFA Non-existent Memory Access
                                                         For o68/o61: DTEs (and backdoor CSR DFA Memory REGION reads)
                                                         have access to the following 38bit L2/DRAM address space
                                                         which maps to a 37bit physical DDR3 SDRAM address space.
                                                         see:
                                                         DR0: 0x0 0000 0000 0000 to 0x0 0000 0FFF FFFF
                                                                 maps to lower 256MB of physical DDR3 SDRAM
                                                         DR1: 0x0 0000 2000 0000 to 0x0 0020 0FFF FFFF
                                                                 maps to upper 127.75GB of DDR3 SDRAM
                                                                    L2/DRAM address space                     Physical DDR3 SDRAM Address space
                                                                      (38bit address)                           (37bit address)
                                                                       +-----------+ 0x0020.0FFF.FFFF
                                                                       |
                                                                      ===   DR1   ===                          +-----------+ 0x001F.FFFF.FFFF
                                                          (128GB-256MB)|           |                           |
                                                                       |           |                     =>    |           |  (128GB-256MB)
                                                                       +-----------+ 0x0000.1FFF.FFFF          |   DR1
                                                               256MB   |   HOLE    |   (DO NOT USE)            |
                                                                       +-----------+ 0x0000.0FFF.FFFF          +-----------+ 0x0000.0FFF.FFFF
                                                               256MB   |    DR0    |                           |   DR0     |   (256MB)
                                                                       +-----------+ 0x0000.0000.0000          +-----------+ 0x0000.0000.0000
                                                         In the event the DFA generates a reference to the L2/DRAM
                                                         address hole (0x0000.0FFF.FFFF - 0x0000.1FFF.FFFF) or to
                                                         an address above 0x0020.0FFF.FFFF, the DFANXM programmable
                                                         interrupt bit will be set.
                                                         SWNOTE: Both the 1) SW DFA Graph compiler and the 2) SW NCB-Direct CSR
                                                         accesses to DFA Memory REGION MUST avoid making references
                                                         to these non-existent memory regions.
                                                         NOTE: If DFANXM is set during a DFA Graph Walk operation,
                                                         then the walk will prematurely terminate with RWORD0[REA]=ERR.
                                                         If DFANXM is set during a NCB-Direct CSR read access to DFA
                                                         Memory REGION, then the CSR read response data is forced to
                                                         128'hBADE_FEED_DEAD_BEEF_FACE_CAFE_BEAD_C0DE. (NOTE: the QW
                                                         being accessed, either the upper or lower QW will be returned). */
	uint64_t cndrd                        : 1;  /**< If any of the cluster's detected a Parity error on RAM1
                                                         this additional bit further specifies that the
                                                         RAM1 parity error was detected during a CND-RD
                                                         (Cache Node Metadata Read).

                                                         For CNDRD Parity Error, the previous CNA arc fetch
                                                         information is written to RWORD1+ as follows:
                                                            RWORD1+[NTYPE]=MNODE
                                                            RWORD1+[NDNID]=cna.ndnid
                                                            RWORD1+[NHMSK]=cna.hmsk
                                                            RWORD1+[NNPTR]=cna.nnptr[13:0]
                                                         NOTE: This bit is set if ANY node cluster's RAM1 accesses
                                                         detect a CNDRD error. */
	uint64_t reserved_14_15               : 2;
	uint64_t dlc0_ovferr                  : 1;  /**< DLC0 Fifo Overflow Error Detected
                                                         This condition should NEVER architecturally occur, and
                                                         is here in case HW credit/debit scheme is not working. */
	uint64_t reserved_4_12                : 9;
	uint64_t dc0perr                      : 3;  /**< Cluster#0 RAM[3:1] Parity Error Detected
                                                         See also DFA_DTCFADR register which contains the
                                                         failing addresses for the internal node cache RAMs. */
	uint64_t dblovf                       : 1;  /**< Doorbell Overflow detected - Status bit
                                                         When set, the 20b accumulated doorbell register
                                                         had overflowed (SW wrote too many doorbell requests).
                                                         If the DBLINA had previously been enabled(set),
                                                         an interrupt will be posted. Software can clear
                                                         the interrupt by writing a 1 to this register bit.
                                                         NOTE: Detection of a Doorbell Register overflow
                                                         is a catastrophic error which may leave the DFA
                                                         HW in an unrecoverable state. */
#else
	uint64_t dblovf                       : 1;
	uint64_t dc0perr                      : 3;
	uint64_t reserved_4_12                : 9;
	uint64_t dlc0_ovferr                  : 1;
	uint64_t reserved_14_15               : 2;
	uint64_t cndrd                        : 1;
	uint64_t dfanxm                       : 1;
	uint64_t replerr                      : 1;
	uint64_t reserved_19_63               : 45;
#endif
	} cn61xx;
	struct cvmx_dfa_error_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_17_63               : 47;
	uint64_t cndrd                        : 1;  /**< If DC0PERR[0]=1 indicating a RAM1 Parity error,
                                                         this additional bit further specifies that the
                                                         RAM1 parity error was detected during a CND-RD
                                                         (Cache Node Metadata Read).

                                                         For CNDRD Parity Error, the previous CNA arc fetch
                                                         information is written to RWORD1+ as follows:
                                                            RWORD1+[NTYPE]=MNODE
                                                            RWORD1+[NDNID]=cna.ndnid
                                                            RWORD1+[NHMSK]=cna.hmsk
                                                            RWORD1+[NNPTR]=cna.nnptr[13:0] */
	uint64_t reserved_4_15                : 12;
	uint64_t dc0perr                      : 3;  /**< RAM[3:1] Parity Error Detected from Node Cluster \#0
                                                         See also DFA_DTCFADR register which contains the
                                                         failing addresses for the internal node cache RAMs. */
	uint64_t dblovf                       : 1;  /**< Doorbell Overflow detected - Status bit
                                                         When set, the 20b accumulated doorbell register
                                                         had overflowed (SW wrote too many doorbell requests).
                                                         If the DBLINA had previously been enabled(set),
                                                         an interrupt will be posted. Software can clear
                                                         the interrupt by writing a 1 to this register bit.
                                                         NOTE: Detection of a Doorbell Register overflow
                                                         is a catastrophic error which may leave the DFA
                                                         HW in an unrecoverable state. */
#else
	uint64_t dblovf                       : 1;
	uint64_t dc0perr                      : 3;
	uint64_t reserved_4_15                : 12;
	uint64_t cndrd                        : 1;
	uint64_t reserved_17_63               : 47;
#endif
	} cn63xx;
	struct cvmx_dfa_error_cn63xx          cn63xxp1;
	struct cvmx_dfa_error_cn63xx          cn66xx;
	struct cvmx_dfa_error_s               cn68xx;
	struct cvmx_dfa_error_s               cn68xxp1;
};
typedef union cvmx_dfa_error cvmx_dfa_error_t;

/**
 * cvmx_dfa_intmsk
 *
 * DFA_INTMSK = DFA ERROR Interrupt Mask Register
 *
 * Description:
 */
union cvmx_dfa_intmsk {
	uint64_t u64;
	struct cvmx_dfa_intmsk_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_19_63               : 45;
	uint64_t replerrena                   : 1;  /**< DFA Illegal Replication Factor Interrupt Enable */
	uint64_t dfanxmena                    : 1;  /**< DFA Non-existent Memory Access Interrupt Enable */
	uint64_t reserved_15_16               : 2;
	uint64_t dlc1_ovfena                  : 1;  /**< DLC1 Fifo Overflow Error Interrupt Enable */
	uint64_t dlc0_ovfena                  : 1;  /**< DLC0 Fifo Overflow Error Interrupt Enable */
	uint64_t reserved_10_12               : 3;
	uint64_t dc2pena                      : 3;  /**< RAM[3:1] Parity Error Enabled Node Cluster \#2 */
	uint64_t dc1pena                      : 3;  /**< RAM[3:1] Parity Error Enabled Node Cluster \#1 */
	uint64_t dc0pena                      : 3;  /**< RAM[3:1] Parity Error Enabled Node Cluster \#0 */
	uint64_t dblina                       : 1;  /**< Doorbell Overflow Interrupt Enable bit.
                                                         When set, doorbell overflow conditions are reported. */
#else
	uint64_t dblina                       : 1;
	uint64_t dc0pena                      : 3;
	uint64_t dc1pena                      : 3;
	uint64_t dc2pena                      : 3;
	uint64_t reserved_10_12               : 3;
	uint64_t dlc0_ovfena                  : 1;
	uint64_t dlc1_ovfena                  : 1;
	uint64_t reserved_15_16               : 2;
	uint64_t dfanxmena                    : 1;
	uint64_t replerrena                   : 1;
	uint64_t reserved_19_63               : 45;
#endif
	} s;
	struct cvmx_dfa_intmsk_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_19_63               : 45;
	uint64_t replerrena                   : 1;  /**< DFA Illegal Replication Factor Interrupt Enable */
	uint64_t dfanxmena                    : 1;  /**< DFA Non-existent Memory Access Interrupt Enable */
	uint64_t reserved_14_16               : 3;
	uint64_t dlc0_ovfena                  : 1;  /**< DLC0 Fifo Overflow Error Interrupt Enable */
	uint64_t reserved_4_12                : 9;
	uint64_t dc0pena                      : 3;  /**< RAM[3:1] Parity Error Enabled Node Cluster \#0 */
	uint64_t dblina                       : 1;  /**< Doorbell Overflow Interrupt Enable bit.
                                                         When set, doorbell overflow conditions are reported. */
#else
	uint64_t dblina                       : 1;
	uint64_t dc0pena                      : 3;
	uint64_t reserved_4_12                : 9;
	uint64_t dlc0_ovfena                  : 1;
	uint64_t reserved_14_16               : 3;
	uint64_t dfanxmena                    : 1;
	uint64_t replerrena                   : 1;
	uint64_t reserved_19_63               : 45;
#endif
	} cn61xx;
	struct cvmx_dfa_intmsk_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t dc0pena                      : 3;  /**< RAM[3:1] Parity Error Enabled Node Cluster \#0 */
	uint64_t dblina                       : 1;  /**< Doorbell Overflow Interrupt Enable bit.
                                                         When set, doorbell overflow conditions are reported. */
#else
	uint64_t dblina                       : 1;
	uint64_t dc0pena                      : 3;
	uint64_t reserved_4_63                : 60;
#endif
	} cn63xx;
	struct cvmx_dfa_intmsk_cn63xx         cn63xxp1;
	struct cvmx_dfa_intmsk_cn63xx         cn66xx;
	struct cvmx_dfa_intmsk_s              cn68xx;
	struct cvmx_dfa_intmsk_s              cn68xxp1;
};
typedef union cvmx_dfa_intmsk cvmx_dfa_intmsk_t;

/**
 * cvmx_dfa_memcfg0
 *
 * DFA_MEMCFG0 = DFA Memory Configuration
 *
 * Description:
 */
union cvmx_dfa_memcfg0 {
	uint64_t u64;
	struct cvmx_dfa_memcfg0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t rldqck90_rst                 : 1;  /**< RLDCK90 and RLDQK90 DLL SW Reset
                                                         When written with a '1' the RLDCK90 and RLDQK90 DLL are
                                                         in soft-reset. */
	uint64_t rldck_rst                    : 1;  /**< RLDCK Zero Delay DLL(Clock Generator) SW Reset
                                                         When written with a '1' the RLDCK zero delay DLL is in
                                                         soft-reset. */
	uint64_t clkdiv                       : 2;  /**< RLDCLK Divisor Select
                                                           - 0: RLDx_CK_H/L = Core Clock /2
                                                           - 1: RESERVED (must not be used)
                                                           - 2: RLDx_CK_H/L = Core Clock /3
                                                           - 3: RLDx_CK_H/L = Core Clock /4
                                                         The DFA LLM interface(s) are tied to the core clock
                                                         frequency through this programmable clock divisor.
                                                         Examples:
                                                            Core Clock(MHz) | DFA-LLM Clock(MHz) | CLKDIV
                                                           -----------------+--------------------+--------
                                                                 800        |    400/(800-DDR)   |  /2
                                                                1000        |    333/(666-DDR)   |  /3
                                                                 800        |    200/(400-DDR)   |  /4
                                                         NOTE: This value MUST BE programmed BEFORE doing a
                                                         Hardware init sequence (see: DFA_MEMCFG0[INIT_Px] bits). */
	uint64_t lpp_ena                      : 1;  /**< PP Linear Port Addressing Mode Enable
                                                         When enabled, PP-core LLM accesses to the lower-512MB
                                                         LLM address space are sent to the single DFA port
                                                         which is enabled. NOTE: If LPP_ENA=1, only
                                                         one DFA RLDRAM port may be enabled for RLDRAM accesses
                                                         (ie: ENA_P0 and ENA_P1 CAN NEVER BOTH be set).
                                                         PP-core LLM accesses to the upper-512MB LLM address
                                                         space are sent to the other 'disabled' DFA port.
                                                         SW RESTRICTION: If LPP_ENA=1, then only one DFA port
                                                         may be enabled for RLDRAM accesses (ie: ENA_P0 and
                                                         ENA_P1 CAN NEVER BOTH be set).
                                                         NOTE: This bit is used to allow PP-Core LLM accesses to a
                                                         disabled port, such that each port can be sequentially
                                                         addressed (ie: disable LW address interleaving).
                                                         Enabling this bit allows BOTH PORTs to be active and
                                                         sequentially addressable. The single port that is
                                                         enabled(ENA_Px) will respond to the low-512MB LLM address
                                                         space, and the other 'disabled' port will respond to the
                                                         high-512MB LLM address space.
                                                         Example usage:
                                                            - DFA RLD0 pins used for TCAM-FPGA(CP2 accesses)
                                                            - DFA RLD1 pins used for RLDRAM (DTE/CP2 accesses).
                                                         USAGE NOTE:
                                                         If LPP_ENA=1 and SW DOES NOT initialize the disabled port
                                                         (ie: INIT_Px=0->1), then refreshes and the HW init
                                                         sequence WILL NOT occur for the disabled port.
                                                         If LPP_ENA=1 and SW does initialize the disabled port
                                                         (INIT_Px=0->1 with ENA_Px=0), then refreshes and
                                                         the HW init sequence WILL occur to the disabled port. */
	uint64_t bunk_init                    : 2;  /**< Controls the CS_N[1:0] during a) a HW Initialization
                                                         sequence (triggered by DFA_MEMCFG0[INIT_Px]) or
                                                         b) during a normal refresh sequence. If
                                                         the BNK_INIT[x]=1, the corresponding CS_N[x] is driven.
                                                         NOTE: This is required for DRAM used in a
                                                         clamshell configuration, since the address lines
                                                         carry Mode Register write data that is unique
                                                         per bunk(or clam). In a clamshell configuration,
                                                         The N3K A[x] pin may be tied into Clam#0's A[x]
                                                         and also into Clam#1's 'mirrored' address bit A[y]
                                                         (eg: Clam0 sees A[5] and Clam1 sees A[15]).
                                                         To support clamshell designs, SW must initiate
                                                         two separate HW init sequences for the two bunks
                                                         (or clams) . Before each HW init sequence is triggered,
                                                         SW must preload the DFA_MEMRLD[22:0] with the data
                                                         that will be driven onto the A[22:0] wires during
                                                         an MRS mode register write.
                                                         NOTE: After the final HW initialization sequence has
                                                         been triggered, SW must wait 64K eclks before writing
                                                         the BUNK_INIT[1:0] field = 3'b11 (so that CS_N[1:0] is
                                                         driven during refresh sequences in normal operation.
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t init_p0                      : 1;  /**< When a '1' is written (and the previous value was '0'),
                                                         the HW init sequence(s) for Memory Port \#0 is
                                                         initiated.
                                                         NOTE: To initialize memory, SW must:
                                                           1) Set up the DFA_MEMCFG0[CLKDIV] ratio for intended
                                                              RLDRAM operation.
                                                                [legal values 0: DIV2 2: DIV3 3: DIV4]
                                                           2) Write a '1' into BOTH the DFA_MEM_CFG0[RLDCK_RST]
                                                              and DFA_MEM_CFG0[RLDQCK90_RST] field at
                                                              the SAME TIME. This step puts all three DLLs in
                                                              SW reset (RLDCK, RLDCK90, RLDQK90 DLLs).
                                                           3) Write a '0' into the DFA_MEM_CFG0[RLDCK_RST] field.
                                                              This step takes the RLDCK DLL out of soft-reset so
                                                              that the DLL can generate the RLDx_CK_H/L clock pins.
                                                           4) Wait 1ms (for RLDCK DLL to achieve lock)
                                                           5) Write a '0' into DFA_MEM_CFG0[RLDQCK90_RST] field.
                                                              This step takes the RLDCK90 DLL AND RLDQK90 DLL out
                                                              of soft-reset.
                                                           6) Wait 1ms (for RLDCK90/RLDQK90 DLLs to achieve lock)
                                                           7) Enable memory port(s):  ENA_P0=1/ENA_P1=1
                                                           8) Wait 100us (to ensure a stable clock
                                                              to the RLDRAMs) - as per RLDRAM spec.
                                                           - - - - - Hardware Initialization Sequence - - - - -
                                                           9) Setup the DFA_MEMCFG0[BUNK_INIT] for the bunk(s)
                                                              intended to be initialized.
                                                          10) Write a '1' to the corresponding INIT_Px which
                                                              will initiate a hardware initialization
                                                              sequence to that'specific' port.
                                                          11) Wait (DFA_MEMCFG0[CLKDIV] * 32K) eclk cycles.
                                                              [to ensure the HW init sequence has completed
                                                              before writing to ANY of the DFA_MEM* registers]
                                                           - - - - - Hardware Initialization Sequence - - - - -
                                                          12) Write the DFA_MEMCFG0[BUNK_INIT]=3 to enable
                                                              refreshes to BOTH bunks.
                                                         NOTE: In some cases (where the address wires are routed
                                                         differently between the front and back 'bunks'),
                                                         SW will need to use DFA_MEMCFG0[BUNK_INIT] bits to
                                                         control the Hardware initialization sequence for a
                                                         'specific bunk'. In these cases, SW would setup the
                                                         BUNK_INIT and repeat Steps \#9-11 for each bunk/port.
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization.
                                                         NOTE: DFA Memory Port#0 corresponds to the Octeon
                                                         RLD0_* pins. */
	uint64_t init_p1                      : 1;  /**< When a '1' is written (and the previous value was '0'),
                                                         the HW init sequence(s) for Memory Port \#1 is
                                                         initiated.
                                                         NOTE: To initialize memory, SW must:
                                                           1) Set up the DFA_MEMCFG0[CLKDIV] ratio for intended
                                                              RLDRAM operation.
                                                                [legal values 0: DIV2 2: DIV3 3: DIV4]
                                                           2) Write a '1' into BOTH the DFA_MEM_CFG0[RLDCK_RST]
                                                              and DFA_MEM_CFG0[RLDQCK90_RST] field at
                                                              the SAME TIME. This step puts all three DLLs in
                                                              SW reset (RLDCK, RLDCK90, RLDQK90 DLLs).
                                                           3) Write a '0' into the DFA_MEM_CFG0[RLDCK_RST] field.
                                                              This step takes the RLDCK DLL out of soft-reset so
                                                              that the DLL can generate the RLDx_CK_H/L clock pins.
                                                           4) Wait 1ms (for RLDCK DLL to achieve lock)
                                                           5) Write a '0' into DFA_MEM_CFG0[RLDQCK90_RST] field.
                                                              This step takes the RLDCK90 DLL AND RLDQK90 DLL out
                                                              of soft-reset.
                                                           6) Wait 1ms (for RLDCK90/RLDQK90 DLLs to achieve lock)
                                                           7) Enable memory port(s) ENA_P0=1/ENA_P1=1
                                                           8) Wait 100us (to ensure a stable clock
                                                              to the RLDRAMs) - as per RLDRAM spec.
                                                           - - - - - Hardware Initialization Sequence - - - - -
                                                           9) Setup the DFA_MEMCFG0[BUNK_INIT] for the bunk(s)
                                                              intended to be initialized.
                                                          10) Write a '1' to the corresponding INIT_Px which
                                                              will initiate a hardware initialization
                                                              sequence to that'specific' port.
                                                          11) Wait (DFA_MEMCFG0[CLKDIV] * 32K) eclk cycles.
                                                              [to ensure the HW init sequence has completed
                                                              before writing to ANY of the DFA_MEM* registers]
                                                           - - - - - Hardware Initialization Sequence - - - - -
                                                          12) Write the DFA_MEMCFG0[BUNK_INIT]=3 to enable
                                                              refreshes to BOTH bunks.
                                                         NOTE: In some cases (where the address wires are routed
                                                         differently between the front and back 'bunks'),
                                                         SW will need to use DFA_MEMCFG0[BUNK_INIT] bits to
                                                         control the Hardware initialization sequence for a
                                                         'specific bunk'. In these cases, SW would setup the
                                                         BUNK_INIT and repeat Steps \#9-11 for each bunk/port.
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization.
                                                         NOTE: DFA Memory Port#1 corresponds to the Octeon
                                                         RLD1_* pins. */
	uint64_t r2r_pbunk                    : 1;  /**< When enabled, an additional command bubble is inserted
                                                         if back to back reads are issued to different physical
                                                         bunks. This is to avoid DQ data bus collisions when
                                                         references cross between physical bunks.
                                                         [NOTE: the physical bunk address boundary is determined
                                                         by the PBUNK bit].
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t pbunk                        : 3;  /**< Physical Bunk address bit pointer.
                                                         Specifies which address bit within the Longword
                                                         Memory address MA[23:0] is used to determine the
                                                         chip selects.
                                                         [RLD_CS0_N corresponds to physical bunk \#0, and
                                                         RLD_CS1_N corresponds to physical bunk \#1].
                                                           - 000: CS0_N = MA[19]/CS1_N = !MA[19]
                                                           - 001: CS0_N = MA[20]/CS1_N = !MA[20]
                                                           - 010: CS0_N = MA[21]/CS1_N = !MA[21]
                                                           - 011: CS0_N = MA[22]/CS1_N = !MA[22]
                                                           - 100: CS0_N = MA[23]/CS1_N = !MA[23]
                                                           - 101-111: CS0_N = 0 /CS1_N = 1
                                                         Example(s):
                                                         To build out a 128MB DFA memory, 4x 32Mx9
                                                         parts could be used to fill out TWO physical
                                                         bunks (clamshell configuration). Each (of the
                                                         two) physical bunks contains 2x 32Mx9 = 16Mx36.
                                                         Each RLDRAM device also contains 8 internal banks,
                                                         therefore the memory Address is 16M/8banks = 2M
                                                         addresses/bunk (2^21). In this case, MA[21] would
                                                         select the physical bunk.
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization.
                                                         be used to determine the Chip Select(s). */
	uint64_t blen                         : 1;  /**< Device Burst Length  (0=2-burst/1=4-burst)
                                                         NOTE: RLDRAM-II MUST USE BLEN=0(2-burst) */
	uint64_t bprch                        : 2;  /**< Tristate Enable (back porch) (\#dclks)
                                                         On reads, allows user to control the shape of the
                                                         tristate disable back porch for the DQ data bus.
                                                         This parameter is also very dependent on the
                                                         RW_DLY and WR_DLY parameters and care must be
                                                         taken when programming these parameters to avoid
                                                         data bus contention. Valid range [0..2]
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t fprch                        : 2;  /**< Tristate Enable (front porch) (\#dclks)
                                                         On reads, allows user to control the shape of the
                                                         tristate disable front porch for the DQ data bus.
                                                         This parameter is also very dependent on the
                                                         RW_DLY and WR_DLY parameters and care must be
                                                         taken when programming these parameters to avoid
                                                         data bus contention. Valid range [0..2]
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t wr_dly                       : 4;  /**< Write->Read CMD Delay (\#mclks):
                                                         Determines \#mclk cycles to insert when controller
                                                         switches from write to read. This allows programmer
                                                         to control the data bus contention.
                                                         For RLDRAM-II(BL2): (TBL=1)
                                                         WR_DLY = ROUND_UP[((TWL+TBL)*2 - TSKW + FPRCH) / 2] - TRL + 1
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization.
                                                         NOTE: For aggressive(performance optimal) designs,
                                                         the WR_DLY 'may' be tuned down(-1) if bus fight
                                                         on W->R transitions is not pronounced. */
	uint64_t rw_dly                       : 4;  /**< Read->Write CMD Delay (\#mclks):
                                                         Determines \#mclk cycles to insert when controller
                                                         switches from read to write. This allows programmer
                                                         to control the data bus contention.
                                                         For RLDRAM-II(BL2): (TBL=1)
                                                         RW_DLY = ROUND_UP[((TRL+TBL)*2 + TSKW + BPRCH+2)/2] - TWL + 1
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization.
                                                         NOTE: For aggressive(performance optimal) designs,
                                                         the RW_DLY 'may' be tuned down(-1) if bus fight
                                                         on R->W transitions is not pronounced. */
	uint64_t sil_lat                      : 2;  /**< Silo Latency (\#dclks): On reads, determines how many
                                                         additional dclks to wait (on top of tRL+1) before
                                                         pulling data out of the padring silos used for time
                                                         domain boundary crossing.
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t mtype                        : 1;  /**< FCRAM-II Memory Type
                                                         *** CN58XX UNSUPPORTED *** */
	uint64_t reserved_2_2                 : 1;
	uint64_t ena_p0                       : 1;  /**< Enable DFA RLDRAM Port#0
                                                         When enabled, this bit lets N3K be the default
                                                         driver for memory port \#0.
                                                         NOTE: a customer is at
                                                         liberty to enable either Port#0 or Port#1 or both.
                                                         NOTE: Once a port has been disabled, it MUST NEVER
                                                         be re-enabled. [the only way to enable a port is
                                                         through a chip reset].
                                                         NOTE: DFA Memory Port#0 corresponds to the Octeon
                                                         RLD0_* pins. */
	uint64_t ena_p1                       : 1;  /**< Enable DFA RLDRAM Port#1
                                                         When enabled, this bit lets N3K be the default
                                                         driver for memory port \#1.
                                                         NOTE: a customer is at
                                                         liberty to enable either Port#0 or Port#1 or both.
                                                         NOTE: Once a port has been disabled, it MUST NEVER
                                                         be re-enabled. [the only way to enable a port is
                                                         through a chip reset].
                                                         NOTE: DFA Memory Port#1 corresponds to the Octeon
                                                         RLD1_* pins. */
#else
	uint64_t ena_p1                       : 1;
	uint64_t ena_p0                       : 1;
	uint64_t reserved_2_2                 : 1;
	uint64_t mtype                        : 1;
	uint64_t sil_lat                      : 2;
	uint64_t rw_dly                       : 4;
	uint64_t wr_dly                       : 4;
	uint64_t fprch                        : 2;
	uint64_t bprch                        : 2;
	uint64_t blen                         : 1;
	uint64_t pbunk                        : 3;
	uint64_t r2r_pbunk                    : 1;
	uint64_t init_p1                      : 1;
	uint64_t init_p0                      : 1;
	uint64_t bunk_init                    : 2;
	uint64_t lpp_ena                      : 1;
	uint64_t clkdiv                       : 2;
	uint64_t rldck_rst                    : 1;
	uint64_t rldqck90_rst                 : 1;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_dfa_memcfg0_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_28_63               : 36;
	uint64_t lpp_ena                      : 1;  /**< PP Linear Port Addressing Mode Enable
                                                         When enabled, PP-core LLM accesses to the lower-512MB
                                                         LLM address space are sent to the single DFA port
                                                         which is enabled. NOTE: If LPP_ENA=1, only
                                                         one DFA RLDRAM port may be enabled for RLDRAM accesses
                                                         (ie: ENA_P0 and ENA_P1 CAN NEVER BOTH be set).
                                                         PP-core LLM accesses to the upper-512MB LLM address
                                                         space are sent to the other 'disabled' DFA port.
                                                         SW RESTRICTION: If LPP_ENA=1, then only one DFA port
                                                         may be enabled for RLDRAM accesses (ie: ENA_P0 and
                                                         ENA_P1 CAN NEVER BOTH be set).
                                                         NOTE: This bit is used to allow PP-Core LLM accesses to a
                                                         disabled port, such that each port can be sequentially
                                                         addressed (ie: disable LW address interleaving).
                                                         Enabling this bit allows BOTH PORTs to be active and
                                                         sequentially addressable. The single port that is
                                                         enabled(ENA_Px) will respond to the low-512MB LLM address
                                                         space, and the other 'disabled' port will respond to the
                                                         high-512MB LLM address space.
                                                         Example usage:
                                                            - DFA RLD0 pins used for TCAM-FPGA(CP2 accesses)
                                                            - DFA RLD1 pins used for RLDRAM (DTE/CP2 accesses).
                                                         USAGE NOTE:
                                                         If LPP_ENA=1 and SW DOES NOT initialize the disabled port
                                                         (ie: INIT_Px=0->1), then refreshes and the HW init
                                                         sequence WILL NOT occur for the disabled port.
                                                         If LPP_ENA=1 and SW does initialize the disabled port
                                                         (INIT_Px=0->1 with ENA_Px=0), then refreshes and
                                                         the HW init sequence WILL occur to the disabled port. */
	uint64_t bunk_init                    : 2;  /**< Controls the CS_N[1:0] during a) a HW Initialization
                                                         sequence (triggered by DFA_MEMCFG0[INIT_Px]) or
                                                         b) during a normal refresh sequence. If
                                                         the BNK_INIT[x]=1, the corresponding CS_N[x] is driven.
                                                         NOTE: This is required for DRAM used in a
                                                         clamshell configuration, since the address lines
                                                         carry Mode Register write data that is unique
                                                         per bunk(or clam). In a clamshell configuration,
                                                         The N3K A[x] pin may be tied into Clam#0's A[x]
                                                         and also into Clam#1's 'mirrored' address bit A[y]
                                                         (eg: Clam0 sees A[5] and Clam1 sees A[15]).
                                                         To support clamshell designs, SW must initiate
                                                         two separate HW init sequences for the two bunks
                                                         (or clams) . Before each HW init sequence is triggered,
                                                         SW must preload the DFA_MEMRLD[22:0] with the data
                                                         that will be driven onto the A[22:0] wires during
                                                         an MRS mode register write.
                                                         NOTE: After the final HW initialization sequence has
                                                         been triggered, SW must wait 64K eclks before writing
                                                         the BUNK_INIT[1:0] field = 3'b11 (so that CS_N[1:0] is
                                                         driven during refresh sequences in normal operation.
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization.
                                                         NOTE: For MTYPE=1(FCRAM) Mode, each bunk MUST BE
                                                         initialized independently. In other words, a HW init
                                                         must be done for Bunk#0, and then another HW init
                                                         must be done for Bunk#1 at power-on. */
	uint64_t init_p0                      : 1;  /**< When a '1' is written (and the previous value was '0'),
                                                         the HW init sequence(s) for Memory Port \#0 is
                                                         initiated.
                                                         NOTE: To initialize memory, SW must:
                                                           1) Enable memory port(s):
                                                               a) ENA_P1=1 (single port in pass 1) OR
                                                               b) ENA_P0=1/ENA_P1=1 (dual ports or single when not pass 1)
                                                           2) Wait 100us (to ensure a stable clock
                                                              to the RLDRAMs) - as per RLDRAM spec.
                                                           3) Write a '1' to the corresponding INIT_Px which
                                                              will initiate a hardware initialization
                                                              sequence.
                                                         NOTE: After writing a '1', SW must wait 64K eclk
                                                         cycles to ensure the HW init sequence has completed
                                                         before writing to ANY of the DFA_MEM* registers.
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization.
                                                         NOTE: DFA Memory Port#0 corresponds to the Octeon
                                                         RLD0_* pins. */
	uint64_t init_p1                      : 1;  /**< When a '1' is written (and the previous value was '0'),
                                                         the HW init sequence(s) for Memory Port \#1 is
                                                         initiated.
                                                         NOTE: To initialize memory, SW must:
                                                           1) Enable memory port(s):
                                                               a) ENA_P1=1 (single port in pass 1) OR
                                                               b) ENA_P0=1/ENA_P1=1 (dual ports or single when not pass 1)
                                                           2) Wait 100us (to ensure a stable clock
                                                              to the RLDRAMs) - as per RLDRAM spec.
                                                           3) Write a '1' to the corresponding INIT_Px which
                                                              will initiate a hardware initialization
                                                              sequence.
                                                         NOTE: After writing a '1', SW must wait 64K eclk
                                                         cycles to ensure the HW init sequence has completed
                                                         before writing to ANY of the DFA_MEM* registers.
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization.
                                                         NOTE: DFA Memory Port#1 corresponds to the Octeon
                                                         RLD1_* pins. */
	uint64_t r2r_pbunk                    : 1;  /**< When enabled, an additional command bubble is inserted
                                                         if back to back reads are issued to different physical
                                                         bunks. This is to avoid DQ data bus collisions when
                                                         references cross between physical bunks.
                                                         [NOTE: the physical bunk address boundary is determined
                                                         by the PBUNK bit].
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization.
                                                         When MTYPE=1(FCRAM)/BLEN=0(2-burst), R2R_PBUNK SHOULD BE
                                                         ZERO(for optimal performance). However, if electrically,
                                                         DQ-sharing becomes a power/heat issue, then R2R_PBUNK
                                                         should be set (but at a cost to performance (1/2 BW). */
	uint64_t pbunk                        : 3;  /**< Physical Bunk address bit pointer.
                                                         Specifies which address bit within the Longword
                                                         Memory address MA[23:0] is used to determine the
                                                         chip selects.
                                                         [RLD_CS0_N corresponds to physical bunk \#0, and
                                                         RLD_CS1_N corresponds to physical bunk \#1].
                                                           - 000: CS0_N = MA[19]/CS1_N = !MA[19]
                                                           - 001: CS0_N = MA[20]/CS1_N = !MA[20]
                                                           - 010: CS0_N = MA[21]/CS1_N = !MA[21]
                                                           - 011: CS0_N = MA[22]/CS1_N = !MA[22]
                                                           - 100: CS0_N = MA[23]/CS1_N = !MA[23]
                                                           - 101-111: CS0_N = 0 /CS1_N = 1
                                                         Example(s):
                                                         To build out a 128MB DFA memory, 4x 32Mx9
                                                         parts could be used to fill out TWO physical
                                                         bunks (clamshell configuration). Each (of the
                                                         two) physical bunks contains 2x 32Mx9 = 16Mx36.
                                                         Each RLDRAM device also contains 8 internal banks,
                                                         therefore the memory Address is 16M/8banks = 2M
                                                         addresses/bunk (2^21). In this case, MA[21] would
                                                         select the physical bunk.
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization.
                                                         be used to determine the Chip Select(s).
                                                         NOTE: When MTYPE=1(FCRAM)/BLEN=0(2-burst), a
                                                         "Redundant Bunk" scheme is employed to provide the
                                                         highest overall performance (1 Req/ MCLK cycle).
                                                         In this mode, it's imperative that SW set the PBUNK
                                                         field +1 'above' the highest address bit. (such that
                                                         the PBUNK extracted from the address will always be
                                                         zero). In this mode, the CS_N[1:0] pins are driven
                                                         to each redundant bunk based on a TDM scheme:
                                                         [MCLK-EVEN=Bunk#0/MCLK-ODD=Bunk#1]. */
	uint64_t blen                         : 1;  /**< Device Burst Length  (0=2-burst/1=4-burst)
                                                         When BLEN=0(BL2), all QW reads/writes from CP2 are
                                                         decomposed into 2 separate BL2(LW) requests to the
                                                         Low-Latency memory.
                                                         When BLEN=1(BL4), a LW request (from CP2 or NCB) is
                                                         treated as 1 BL4(QW) request to the low latency memory.
                                                         NOTE: QW refers to a 64-bit LLM Load/Store (intiated
                                                         by a processor core). LW refers to a 36-bit load/store.
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization before the DFA LLM
                                                         (low latency memory) is used.
                                                         NOTE: MTYPE=0(RLDRAM-II) MUST USE BLEN=0(2-burst)
                                                         NOTE: MTYPE=1(FCRAM)/BLEN=0(BL2) requires a
                                                         multi-bunk(clam) board design.
                                                         NOTE: If MTYPE=1(FCRAM)/FCRAM2P=0(II)/BLEN=1(BL4),
                                                         SW SHOULD use CP2 QW read/write requests (for
                                                         optimal low-latency bus performance).
                                                         [LW length read/write requests(in BL4 mode) use 50%
                                                         of the available bus bandwidth]
                                                         NOTE: MTYPE=1(FCRAM)/FCRAM2P=0(II)/BLEN=0(BL2) can only
                                                         be used with FCRAM-II devices which support BL2 mode
                                                         (see: Toshiba FCRAM-II, where DQ tristate after 2 data
                                                         transfers).
                                                         NOTE: MTYPE=1(FCRAM)/FCRAM2P=1(II+) does not support LW
                                                         write requests (FCRAM-II+ device specification has removed
                                                         the variable write mask function from the devices).
                                                         As such, if this mode is used, SW must be careful to
                                                         issue only PP-CP2 QW write requests. */
	uint64_t bprch                        : 2;  /**< Tristate Enable (back porch) (\#dclks)
                                                         On reads, allows user to control the shape of the
                                                         tristate disable back porch for the DQ data bus.
                                                         This parameter is also very dependent on the
                                                         RW_DLY and WR_DLY parameters and care must be
                                                         taken when programming these parameters to avoid
                                                         data bus contention. Valid range [0..2]
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t fprch                        : 2;  /**< Tristate Enable (front porch) (\#dclks)
                                                         On reads, allows user to control the shape of the
                                                         tristate disable front porch for the DQ data bus.
                                                         This parameter is also very dependent on the
                                                         RW_DLY and WR_DLY parameters and care must be
                                                         taken when programming these parameters to avoid
                                                         data bus contention. Valid range [0..2]
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t wr_dly                       : 4;  /**< Write->Read CMD Delay (\#mclks):
                                                         Determines \#mclk cycles to insert when controller
                                                         switches from write to read. This allows programmer
                                                         to control the data bus contention.
                                                         For RLDRAM-II(BL2): (TBL=1)
                                                         For FCRAM-II (BL4): (TBL=2)
                                                         For FCRAM-II (BL2 grepl=1x ONLY): (TBL=1)
                                                         For FCRAM-II (BL2 grepl>=2x): (TBL=3)
                                                            NOTE: When MTYTPE=1(FCRAM-II) BLEN=0(BL2 Mode),
                                                            grepl>=2x, writes require redundant bunk writes
                                                            which require an additional 2 cycles before slotting
                                                            the next read.
                                                         WR_DLY = ROUND_UP[((TWL+TBL)*2 - TSKW + FPRCH) / 2] - TRL + 1
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization.
                                                         NOTE: For aggressive(performance optimal) designs,
                                                         the WR_DLY 'may' be tuned down(-1) if bus fight
                                                         on W->R transitions is not pronounced. */
	uint64_t rw_dly                       : 4;  /**< Read->Write CMD Delay (\#mclks):
                                                         Determines \#mclk cycles to insert when controller
                                                         switches from read to write. This allows programmer
                                                         to control the data bus contention.
                                                         For RLDRAM-II/FCRAM-II (BL2): (TBL=1)
                                                         For FCRAM-II (BL4): (TBL=2)
                                                         RW_DLY = ROUND_UP[((TRL+TBL)*2 + TSKW + BPRCH+2)/2] - TWL + 1
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization.
                                                         NOTE: For aggressive(performance optimal) designs,
                                                         the RW_DLY 'may' be tuned down(-1) if bus fight
                                                         on R->W transitions is not pronounced. */
	uint64_t sil_lat                      : 2;  /**< Silo Latency (\#dclks): On reads, determines how many
                                                         additional dclks to wait (on top of tRL+1) before
                                                         pulling data out of the padring silos used for time
                                                         domain boundary crossing.
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t mtype                        : 1;  /**< Memory Type (0=RLDRAM-II/1=Network DRAM-II/FCRAM)
                                                         NOTE: N3K-P1 only supports RLDRAM-II
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization.
                                                         NOTE: When MTYPE=1(FCRAM)/BLEN=0(2-burst), only the
                                                         "unidirectional DS/QS" mode is supported. (see FCRAM
                                                         data sheet EMRS[A6:A5]=SS(Strobe Select) register
                                                         definition. [in FCRAM 2-burst mode, we use FCRAM
                                                         in a clamshell configuration such that clam0 is
                                                         addressed independently of clam1, and DQ is shared
                                                         for optimal performance. As such it's imperative that
                                                         the QS are conditionally received (and are NOT
                                                         free-running), as the N3K receive data capture silos
                                                         OR the clam0/1 QS strobes.
                                                         NOTE: If this bit is SET, the ASX0/1
                                                         ASX_RLD_FCRAM_MODE[MODE] bit(s) should also be SET
                                                         in order for the RLD0/1-PHY(s) to support FCRAM devices. */
	uint64_t reserved_2_2                 : 1;
	uint64_t ena_p0                       : 1;  /**< Enable DFA RLDRAM Port#0
                                                         When enabled, this bit lets N3K be the default
                                                         driver for memory port \#0.
                                                         NOTE: For N3K-P1, to enable Port#0(2nd port),
                                                         Port#1 MUST ALSO be enabled.
                                                         NOTE: For N3K-P2, single port mode, a customer is at
                                                         liberty to enable either Port#0 or Port#1.
                                                         NOTE: Once a port has been disabled, it MUST NEVER
                                                         be re-enabled. [the only way to enable a port is
                                                         through a chip reset].
                                                         NOTE: DFA Memory Port#0 corresponds to the Octeon
                                                         RLD0_* pins. */
	uint64_t ena_p1                       : 1;  /**< Enable DFA RLDRAM Port#1
                                                         When enabled, this bit lets N3K be the default
                                                         driver for memory port \#1.
                                                         NOTE: For N3K-P1, If the customer wishes to use a
                                                         single port, s/he must enable Port#1 (and not Port#0).
                                                         NOTE: For N3K-P2, single port mode, a customer is at
                                                         liberty to enable either Port#0 or Port#1.
                                                         NOTE: Once a port has been disabled, it MUST NEVER
                                                         be re-enabled. [the only way to enable a port is
                                                         through a chip reset].
                                                         NOTE: DFA Memory Port#1 corresponds to the Octeon
                                                         RLD1_* pins. */
#else
	uint64_t ena_p1                       : 1;
	uint64_t ena_p0                       : 1;
	uint64_t reserved_2_2                 : 1;
	uint64_t mtype                        : 1;
	uint64_t sil_lat                      : 2;
	uint64_t rw_dly                       : 4;
	uint64_t wr_dly                       : 4;
	uint64_t fprch                        : 2;
	uint64_t bprch                        : 2;
	uint64_t blen                         : 1;
	uint64_t pbunk                        : 3;
	uint64_t r2r_pbunk                    : 1;
	uint64_t init_p1                      : 1;
	uint64_t init_p0                      : 1;
	uint64_t bunk_init                    : 2;
	uint64_t lpp_ena                      : 1;
	uint64_t reserved_28_63               : 36;
#endif
	} cn38xx;
	struct cvmx_dfa_memcfg0_cn38xxp2 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_27_63               : 37;
	uint64_t bunk_init                    : 2;  /**< Controls the CS_N[1:0] during a) a HW Initialization
                                                         sequence (triggered by DFA_MEMCFG0[INIT_Px]) or
                                                         b) during a normal refresh sequence. If
                                                         the BNK_INIT[x]=1, the corresponding CS_N[x] is driven.
                                                         NOTE: This is required for DRAM used in a
                                                         clamshell configuration, since the address lines
                                                         carry Mode Register write data that is unique
                                                         per bunk(or clam). In a clamshell configuration,
                                                         The N3K A[x] pin may be tied into Clam#0's A[x]
                                                         and also into Clam#1's 'mirrored' address bit A[y]
                                                         (eg: Clam0 sees A[5] and Clam1 sees A[15]).
                                                         To support clamshell designs, SW must initiate
                                                         two separate HW init sequences for the two bunks
                                                         (or clams) . Before each HW init sequence is triggered,
                                                         SW must preload the DFA_MEMRLD[22:0] with the data
                                                         that will be driven onto the A[22:0] wires during
                                                         an MRS mode register write.
                                                         NOTE: After the final HW initialization sequence has
                                                         been triggered, SW must wait 64K eclks before writing
                                                         the BUNK_INIT[1:0] field = 3'b11 (so that CS_N[1:0] is
                                                         driven during refresh sequences in normal operation.
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization.
                                                         NOTE: For MTYPE=1(FCRAM) Mode, each bunk MUST BE
                                                         initialized independently. In other words, a HW init
                                                         must be done for Bunk#0, and then another HW init
                                                         must be done for Bunk#1 at power-on. */
	uint64_t init_p0                      : 1;  /**< When a '1' is written (and the previous value was '0'),
                                                         the HW init sequence(s) for Memory Port \#0 is
                                                         initiated.
                                                         NOTE: To initialize memory, SW must:
                                                           1) Enable memory port(s):
                                                               a) ENA_P1=1 (single port in pass 1) OR
                                                               b) ENA_P0=1/ENA_P1=1 (dual ports or single when not pass 1)
                                                           2) Wait 100us (to ensure a stable clock
                                                              to the RLDRAMs) - as per RLDRAM spec.
                                                           3) Write a '1' to the corresponding INIT_Px which
                                                              will initiate a hardware initialization
                                                              sequence.
                                                         NOTE: After writing a '1', SW must wait 64K eclk
                                                         cycles to ensure the HW init sequence has completed
                                                         before writing to ANY of the DFA_MEM* registers.
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization.
                                                         NOTE: DFA Memory Port#0 corresponds to the Octeon
                                                         RLD0_* pins. */
	uint64_t init_p1                      : 1;  /**< When a '1' is written (and the previous value was '0'),
                                                         the HW init sequence(s) for Memory Port \#1 is
                                                         initiated.
                                                         NOTE: To initialize memory, SW must:
                                                           1) Enable memory port(s):
                                                               a) ENA_P1=1 (single port in pass 1) OR
                                                               b) ENA_P0=1/ENA_P1=1 (dual ports or single when not pass 1)
                                                           2) Wait 100us (to ensure a stable clock
                                                              to the RLDRAMs) - as per RLDRAM spec.
                                                           3) Write a '1' to the corresponding INIT_Px which
                                                              will initiate a hardware initialization
                                                              sequence.
                                                         NOTE: After writing a '1', SW must wait 64K eclk
                                                         cycles to ensure the HW init sequence has completed
                                                         before writing to ANY of the DFA_MEM* registers.
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization.
                                                         NOTE: DFA Memory Port#1 corresponds to the Octeon
                                                         RLD1_* pins. */
	uint64_t r2r_pbunk                    : 1;  /**< When enabled, an additional command bubble is inserted
                                                         if back to back reads are issued to different physical
                                                         bunks. This is to avoid DQ data bus collisions when
                                                         references cross between physical bunks.
                                                         [NOTE: the physical bunk address boundary is determined
                                                         by the PBUNK bit].
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization.
                                                         When MTYPE=1(FCRAM)/BLEN=0(2-burst), R2R_PBUNK SHOULD BE
                                                         ZERO(for optimal performance). However, if electrically,
                                                         DQ-sharing becomes a power/heat issue, then R2R_PBUNK
                                                         should be set (but at a cost to performance (1/2 BW). */
	uint64_t pbunk                        : 3;  /**< Physical Bunk address bit pointer.
                                                         Specifies which address bit within the Longword
                                                         Memory address MA[23:0] is used to determine the
                                                         chip selects.
                                                         [RLD_CS0_N corresponds to physical bunk \#0, and
                                                         RLD_CS1_N corresponds to physical bunk \#1].
                                                           - 000: CS0_N = MA[19]/CS1_N = !MA[19]
                                                           - 001: CS0_N = MA[20]/CS1_N = !MA[20]
                                                           - 010: CS0_N = MA[21]/CS1_N = !MA[21]
                                                           - 011: CS0_N = MA[22]/CS1_N = !MA[22]
                                                           - 100: CS0_N = MA[23]/CS1_N = !MA[23]
                                                           - 101-111: CS0_N = 0 /CS1_N = 1
                                                         Example(s):
                                                         To build out a 128MB DFA memory, 4x 32Mx9
                                                         parts could be used to fill out TWO physical
                                                         bunks (clamshell configuration). Each (of the
                                                         two) physical bunks contains 2x 32Mx9 = 16Mx36.
                                                         Each RLDRAM device also contains 8 internal banks,
                                                         therefore the memory Address is 16M/8banks = 2M
                                                         addresses/bunk (2^21). In this case, MA[21] would
                                                         select the physical bunk.
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization.
                                                         be used to determine the Chip Select(s).
                                                         NOTE: When MTYPE=1(FCRAM)/BLEN=0(2-burst), a
                                                         "Redundant Bunk" scheme is employed to provide the
                                                         highest overall performance (1 Req/ MCLK cycle).
                                                         In this mode, it's imperative that SW set the PBUNK
                                                         field +1 'above' the highest address bit. (such that
                                                         the PBUNK extracted from the address will always be
                                                         zero). In this mode, the CS_N[1:0] pins are driven
                                                         to each redundant bunk based on a TDM scheme:
                                                         [MCLK-EVEN=Bunk#0/MCLK-ODD=Bunk#1]. */
	uint64_t blen                         : 1;  /**< Device Burst Length  (0=2-burst/1=4-burst)
                                                         When BLEN=0(BL2), all QW reads/writes from CP2 are
                                                         decomposed into 2 separate BL2(LW) requests to the
                                                         Low-Latency memory.
                                                         When BLEN=1(BL4), a LW request (from CP2 or NCB) is
                                                         treated as 1 BL4(QW) request to the low latency memory.
                                                         NOTE: QW refers to a 64-bit LLM Load/Store (intiated
                                                         by a processor core). LW refers to a 36-bit load/store.
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization before the DFA LLM
                                                         (low latency memory) is used.
                                                         NOTE: MTYPE=0(RLDRAM-II) MUST USE BLEN=0(2-burst)
                                                         NOTE: MTYPE=1(FCRAM)/BLEN=0(BL2) requires a
                                                         multi-bunk(clam) board design.
                                                         NOTE: If MTYPE=1(FCRAM)/FCRAM2P=0(II)/BLEN=1(BL4),
                                                         SW SHOULD use CP2 QW read/write requests (for
                                                         optimal low-latency bus performance).
                                                         [LW length read/write requests(in BL4 mode) use 50%
                                                         of the available bus bandwidth]
                                                         NOTE: MTYPE=1(FCRAM)/FCRAM2P=0(II)/BLEN=0(BL2) can only
                                                         be used with FCRAM-II devices which support BL2 mode
                                                         (see: Toshiba FCRAM-II, where DQ tristate after 2 data
                                                         transfers).
                                                         NOTE: MTYPE=1(FCRAM)/FCRAM2P=1(II+) does not support LW
                                                         write requests (FCRAM-II+ device specification has removed
                                                         the variable write mask function from the devices).
                                                         As such, if this mode is used, SW must be careful to
                                                         issue only PP-CP2 QW write requests. */
	uint64_t bprch                        : 2;  /**< Tristate Enable (back porch) (\#dclks)
                                                         On reads, allows user to control the shape of the
                                                         tristate disable back porch for the DQ data bus.
                                                         This parameter is also very dependent on the
                                                         RW_DLY and WR_DLY parameters and care must be
                                                         taken when programming these parameters to avoid
                                                         data bus contention. Valid range [0..2]
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t fprch                        : 2;  /**< Tristate Enable (front porch) (\#dclks)
                                                         On reads, allows user to control the shape of the
                                                         tristate disable front porch for the DQ data bus.
                                                         This parameter is also very dependent on the
                                                         RW_DLY and WR_DLY parameters and care must be
                                                         taken when programming these parameters to avoid
                                                         data bus contention. Valid range [0..2]
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t wr_dly                       : 4;  /**< Write->Read CMD Delay (\#mclks):
                                                         Determines \#mclk cycles to insert when controller
                                                         switches from write to read. This allows programmer
                                                         to control the data bus contention.
                                                         For RLDRAM-II(BL2): (TBL=1)
                                                         For FCRAM-II (BL4): (TBL=2)
                                                         For FCRAM-II (BL2 grepl=1x ONLY): (TBL=1)
                                                         For FCRAM-II (BL2 grepl>=2x): (TBL=3)
                                                            NOTE: When MTYTPE=1(FCRAM-II) BLEN=0(BL2 Mode),
                                                            grepl>=2x, writes require redundant bunk writes
                                                            which require an additional 2 cycles before slotting
                                                            the next read.
                                                         WR_DLY = ROUND_UP[((TWL+TBL)*2 - TSKW + FPRCH) / 2] - TRL + 1
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization.
                                                         NOTE: For aggressive(performance optimal) designs,
                                                         the WR_DLY 'may' be tuned down(-1) if bus fight
                                                         on W->R transitions is not pronounced. */
	uint64_t rw_dly                       : 4;  /**< Read->Write CMD Delay (\#mclks):
                                                         Determines \#mclk cycles to insert when controller
                                                         switches from read to write. This allows programmer
                                                         to control the data bus contention.
                                                         For RLDRAM-II/FCRAM-II (BL2): (TBL=1)
                                                         For FCRAM-II (BL4): (TBL=2)
                                                         RW_DLY = ROUND_UP[((TRL+TBL)*2 + TSKW + BPRCH+2)/2] - TWL + 1
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization.
                                                         NOTE: For aggressive(performance optimal) designs,
                                                         the RW_DLY 'may' be tuned down(-1) if bus fight
                                                         on R->W transitions is not pronounced. */
	uint64_t sil_lat                      : 2;  /**< Silo Latency (\#dclks): On reads, determines how many
                                                         additional dclks to wait (on top of tRL+1) before
                                                         pulling data out of the padring silos used for time
                                                         domain boundary crossing.
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t mtype                        : 1;  /**< Memory Type (0=RLDRAM-II/1=Network DRAM-II/FCRAM)
                                                         NOTE: N3K-P1 only supports RLDRAM-II
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization.
                                                         NOTE: When MTYPE=1(FCRAM)/BLEN=0(2-burst), only the
                                                         "unidirectional DS/QS" mode is supported. (see FCRAM
                                                         data sheet EMRS[A6:A5]=SS(Strobe Select) register
                                                         definition. [in FCRAM 2-burst mode, we use FCRAM
                                                         in a clamshell configuration such that clam0 is
                                                         addressed independently of clam1, and DQ is shared
                                                         for optimal performance. As such it's imperative that
                                                         the QS are conditionally received (and are NOT
                                                         free-running), as the N3K receive data capture silos
                                                         OR the clam0/1 QS strobes.
                                                         NOTE: If this bit is SET, the ASX0/1
                                                         ASX_RLD_FCRAM_MODE[MODE] bit(s) should also be SET
                                                         in order for the RLD0/1-PHY(s) to support FCRAM devices. */
	uint64_t reserved_2_2                 : 1;
	uint64_t ena_p0                       : 1;  /**< Enable DFA RLDRAM Port#0
                                                         When enabled, this bit lets N3K be the default
                                                         driver for memory port \#0.
                                                         NOTE: For N3K-P1, to enable Port#0(2nd port),
                                                         Port#1 MUST ALSO be enabled.
                                                         NOTE: For N3K-P2, single port mode, a customer is at
                                                         liberty to enable either Port#0 or Port#1.
                                                         NOTE: Once a port has been disabled, it MUST NEVER
                                                         be re-enabled. [the only way to enable a port is
                                                         through a chip reset].
                                                         NOTE: DFA Memory Port#0 corresponds to the Octeon
                                                         RLD0_* pins. */
	uint64_t ena_p1                       : 1;  /**< Enable DFA RLDRAM Port#1
                                                         When enabled, this bit lets N3K be the default
                                                         driver for memory port \#1.
                                                         NOTE: For N3K-P1, If the customer wishes to use a
                                                         single port, s/he must enable Port#1 (and not Port#0).
                                                         NOTE: For N3K-P2, single port mode, a customer is at
                                                         liberty to enable either Port#0 or Port#1.
                                                         NOTE: Once a port has been disabled, it MUST NEVER
                                                         be re-enabled. [the only way to enable a port is
                                                         through a chip reset].
                                                         NOTE: DFA Memory Port#1 corresponds to the Octeon
                                                         RLD1_* pins. */
#else
	uint64_t ena_p1                       : 1;
	uint64_t ena_p0                       : 1;
	uint64_t reserved_2_2                 : 1;
	uint64_t mtype                        : 1;
	uint64_t sil_lat                      : 2;
	uint64_t rw_dly                       : 4;
	uint64_t wr_dly                       : 4;
	uint64_t fprch                        : 2;
	uint64_t bprch                        : 2;
	uint64_t blen                         : 1;
	uint64_t pbunk                        : 3;
	uint64_t r2r_pbunk                    : 1;
	uint64_t init_p1                      : 1;
	uint64_t init_p0                      : 1;
	uint64_t bunk_init                    : 2;
	uint64_t reserved_27_63               : 37;
#endif
	} cn38xxp2;
	struct cvmx_dfa_memcfg0_s             cn58xx;
	struct cvmx_dfa_memcfg0_s             cn58xxp1;
};
typedef union cvmx_dfa_memcfg0 cvmx_dfa_memcfg0_t;

/**
 * cvmx_dfa_memcfg1
 *
 * DFA_MEMCFG1 = RLDRAM Memory Timing Configuration
 *
 * Description:
 */
union cvmx_dfa_memcfg1 {
	uint64_t u64;
	struct cvmx_dfa_memcfg1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t ref_intlo                    : 9;  /**< Burst Refresh Interval[8:0] (\#dclks)
                                                         For finer refresh interval granularity control.
                                                         This field provides an additional level of granularity
                                                         for the refresh interval. It specifies the additional
                                                         \#dclks [0...511] to be added to the REF_INT[3:0] field.
                                                         For RLDRAM-II: For dclk(400MHz=2.5ns):
                                                         Example: 64K AREF cycles required within tREF=32ms
                                                             trefint = tREF(ms)/(64K cycles/8banks)
                                                                         = 32ms/8K = 3.9us = 3900ns
                                                             REF_INT[3:0] = ROUND_DOWN[(trefint/dclk)/512]
                                                                          = ROUND_DOWN[(3900/2.5)/512]
                                                                          = 3
                                                             REF_INTLO[8:0] = MOD[(trefint/dclk)/512]
                                                                            = MOD[(3900/2.5)/512]
                                                                            = 24
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t aref_ena                     : 1;  /**< Auto Refresh Cycle Enable
                                                         INTERNAL USE ONLY:
                                                         NOTE: This mode bit is ONLY intended to be used by
                                                         low-level power-on initialization routines in the
                                                         event that the hardware initialization routine
                                                         does not work. It allows SW to create AREF
                                                         commands on the RLDRAM bus directly.
                                                         When this bit is set, ALL RLDRAM writes (issued by
                                                         a PP through the NCB or CP2) are converted to AREF
                                                         commands on the RLDRAM bus. The write-address is
                                                         presented on the A[20:0]/BA[2:0] pins (for which
                                                         the RLDRAM only interprets BA[2:0]).
                                                         When this bit is set, only writes are allowed
                                                         and MUST use grepl=0 (1x).
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization.
                                                         NOTE: MRS_ENA and AREF_ENA are mutually exclusive
                                                         (SW can set one or the other, but never both!)
                                                         NOTE: AREF commands generated using this method target
                                                         the 'addressed' bunk. */
	uint64_t mrs_ena                      : 1;  /**< Mode Register Set Cycle Enable
                                                         INTERNAL USE ONLY:
                                                         NOTE: This mode bit is ONLY intended to be used by
                                                         low-level power-on initialization routines in the
                                                         event that the hardware initialization routine
                                                         does not work. It allows SW to create MRS
                                                         commands on the RLDRAM bus directly.
                                                         When this bit is set, ALL RLDRAM writes (issued by
                                                         a PP through the NCB or CP2) are converted to MRS
                                                         commands on the RLDRAM bus. The write-address is
                                                         presented on the A[20:0]/BA[2:0] pins (for which
                                                         the RLDRAM only interprets A[17:0]).
                                                         When this bit is set, only writes are allowed
                                                         and MUST use grepl=0 (1x).
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization.
                                                         NOTE: MRS_ENA and AREF_ENA are mutually exclusive
                                                         (SW can set one or the other, but never both!)
                                                         NOTE: MRS commands generated using this method target
                                                         the 'addressed' bunk. */
	uint64_t tmrsc                        : 3;  /**< Mode Register Set Cycle Time (represented in \#mclks)
                                                              - 000-001: RESERVED
                                                              - 010: tMRSC = 2 mclks
                                                              - 011: tMRSC = 3 mclks
                                                              - ...
                                                              - 111: tMRSC = 7 mclks
                                                         NOTE: The device tMRSC parameter is a function of CL
                                                         (which during HW initialization is not known. Its
                                                         recommended to load tMRSC(MAX) value to avoid timing
                                                         violations.
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t trc                          : 4;  /**< Row Cycle Time (represented in \#mclks)
                                                         see also: DFA_MEMRLD[RLCFG] field which must
                                                         correspond with tRL/tWL parameter(s).
                                                              - 0000-0010: RESERVED
                                                              - 0011: tRC = 3 mclks
                                                              - 0100: tRC = 4 mclks
                                                              - 0101: tRC = 5 mclks
                                                              - 0110: tRC = 6 mclks
                                                              - 0111: tRC = 7 mclks
                                                              - 1000: tRC = 8 mclks
                                                              - 1001: tRC = 9 mclks
                                                              - 1010-1111: RESERVED
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t twl                          : 4;  /**< Write Latency (represented in \#mclks)
                                                         see also: DFA_MEMRLD[RLCFG] field which must
                                                         correspond with tRL/tWL parameter(s).
                                                              - 0000-0001: RESERVED
                                                              - 0010: Write Latency (WL=2.0 mclk)
                                                              - 0011: Write Latency (WL=3.0 mclks)
                                                              - 0100: Write Latency (WL=4.0 mclks)
                                                              - 0101: Write Latency (WL=5.0 mclks)
                                                              - 0110: Write Latency (WL=6.0 mclks)
                                                              - 0111: Write Latency (WL=7.0 mclks)
                                                              - 1000: Write Latency (WL=8.0 mclks)
                                                              - 1001: Write Latency (WL=9.0 mclks)
                                                              - 1010: Write Latency (WL=10.0 mclks)
                                                              - 1011-1111: RESERVED
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t trl                          : 4;  /**< Read Latency (represented in \#mclks)
                                                         see also: DFA_MEMRLD[RLCFG] field which must
                                                         correspond with tRL/tWL parameter(s).
                                                              - 0000-0010: RESERVED
                                                              - 0011: Read Latency = 3 mclks
                                                              - 0100: Read Latency = 4 mclks
                                                              - 0101: Read Latency = 5 mclks
                                                              - 0110: Read Latency = 6 mclks
                                                              - 0111: Read Latency = 7 mclks
                                                              - 1000: Read Latency = 8 mclks
                                                              - 1001: Read Latency = 9 mclks
                                                              - 1010: Read Latency = 10 mclks
                                                              - 1011-1111: RESERVED
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t reserved_6_7                 : 2;
	uint64_t tskw                         : 2;  /**< Board Skew (represented in \#dclks)
                                                         Represents additional board skew of DQ/DQS.
                                                             - 00: board-skew = 0 dclk
                                                             - 01: board-skew = 1 dclk
                                                             - 10: board-skew = 2 dclk
                                                             - 11: board-skew = 3 dclk
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t ref_int                      : 4;  /**< Refresh Interval (represented in \#of 512 dclk
                                                         increments).
                                                              - 0000: RESERVED
                                                              - 0001: 1 * 512  = 512 dclks
                                                              - ...
                                                              - 1111: 15 * 512 = 7680 dclks
                                                         NOTE: For finer level of granularity, refer to
                                                         REF_INTLO[8:0] field.
                                                         For RLDRAM-II, each refresh interval will
                                                         generate a burst of 8 AREF commands, one to each of
                                                         8 explicit banks (referenced using the RLD_BA[2:0]
                                                         pins.
                                                         Example: For mclk=200MHz/dclk(400MHz=2.5ns):
                                                           64K AREF cycles required within tREF=32ms
                                                             trefint = tREF(ms)/(64K cycles/8banks)
                                                                     = 32ms/8K = 3.9us = 3900ns
                                                             REF_INT = ROUND_DOWN[(trefint/dclk)/512]
                                                                     = ROUND_DOWN[(3900/2.5)/512]
                                                                     = 3
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
#else
	uint64_t ref_int                      : 4;
	uint64_t tskw                         : 2;
	uint64_t reserved_6_7                 : 2;
	uint64_t trl                          : 4;
	uint64_t twl                          : 4;
	uint64_t trc                          : 4;
	uint64_t tmrsc                        : 3;
	uint64_t mrs_ena                      : 1;
	uint64_t aref_ena                     : 1;
	uint64_t ref_intlo                    : 9;
	uint64_t reserved_34_63               : 30;
#endif
	} s;
	struct cvmx_dfa_memcfg1_s             cn38xx;
	struct cvmx_dfa_memcfg1_s             cn38xxp2;
	struct cvmx_dfa_memcfg1_s             cn58xx;
	struct cvmx_dfa_memcfg1_s             cn58xxp1;
};
typedef union cvmx_dfa_memcfg1 cvmx_dfa_memcfg1_t;

/**
 * cvmx_dfa_memcfg2
 *
 * DFA_MEMCFG2 = DFA Memory Config Register \#2
 * *** NOTE: Pass2 Addition
 *
 * Description: Additional Memory Configuration CSRs to support FCRAM-II/II+ and Network DRAM-II
 */
union cvmx_dfa_memcfg2 {
	uint64_t u64;
	struct cvmx_dfa_memcfg2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_12_63               : 52;
	uint64_t dteclkdis                    : 1;  /**< DFA DTE Clock Disable
                                                         When SET, the DFA clocks for DTE(thread engine)
                                                         operation are disabled.
                                                         NOTE: When SET, SW MUST NEVER issue ANY operations to
                                                         the DFA via the NCB Bus. All DFA Operations must be
                                                         issued solely through the CP2 interface.

                                                         NOTE: When DTECLKDIS=1, if CP2 Errors are encountered
                                                         (ie: CP2SBE, CP2DBE, CP2PERR), the DFA_MEMFADR CSR
                                                         does not reflect the failing address/ctl information. */
	uint64_t silrst                       : 1;  /**< LLM-PHY Silo Reset
                                                         When a '1' is written (when the previous
                                                         value was a '0') causes the the LLM-PHY Silo read/write
                                                         pointers to be reset.
                                                         NOTE: SW MUST WAIT 400 dclks after the LAST HW Init
                                                         sequence was launched (ie: INIT_START 0->1 CSR write),
                                                         before the SILRST can be triggered (0->1). */
	uint64_t trfc                         : 5;  /**< FCRAM-II Refresh Interval
                                                         *** CN58XX UNSUPPORTED *** */
	uint64_t refshort                     : 1;  /**< FCRAM Short Refresh Mode
                                                         *** CN58XX UNSUPPORTED *** */
	uint64_t ua_start                     : 2;  /**< FCRAM-II Upper Addres Start
                                                         *** CN58XX UNSUPPORTED *** */
	uint64_t maxbnk                       : 1;  /**< Maximum Banks per-device (used by the address mapper
                                                         when extracting address bits for the memory bank#.
                                                           - 0: 4 banks/device
                                                           - 1: 8 banks/device */
	uint64_t fcram2p                      : 1;  /**< FCRAM-II+ Mode Enable
                                                         *** CN58XX UNSUPPORTED *** */
#else
	uint64_t fcram2p                      : 1;
	uint64_t maxbnk                       : 1;
	uint64_t ua_start                     : 2;
	uint64_t refshort                     : 1;
	uint64_t trfc                         : 5;
	uint64_t silrst                       : 1;
	uint64_t dteclkdis                    : 1;
	uint64_t reserved_12_63               : 52;
#endif
	} s;
	struct cvmx_dfa_memcfg2_s             cn38xx;
	struct cvmx_dfa_memcfg2_s             cn38xxp2;
	struct cvmx_dfa_memcfg2_s             cn58xx;
	struct cvmx_dfa_memcfg2_s             cn58xxp1;
};
typedef union cvmx_dfa_memcfg2 cvmx_dfa_memcfg2_t;

/**
 * cvmx_dfa_memfadr
 *
 * DFA_MEMFADR = RLDRAM Failing Address/Control Register
 *
 * Description: DFA Memory Failing Address/Control Error Capture information
 * This register contains useful information to help in isolating an RLDRAM memory failure.
 * NOTE: The first detected SEC/DED/PERR failure is captured in DFA_MEMFADR, however, a DED or PERR (which is
 * more severe) will always overwrite a SEC error. The user can 'infer' the source of the interrupt
 * via the FSRC field.
 * NOTE: If DFA_MEMCFG2[DTECLKDIS]=1, the contents of this register are UNDEFINED.
 */
union cvmx_dfa_memfadr {
	uint64_t u64;
	struct cvmx_dfa_memfadr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_24_63               : 40;
	uint64_t maddr                        : 24; /**< Memory Address */
#else
	uint64_t maddr                        : 24;
	uint64_t reserved_24_63               : 40;
#endif
	} s;
	struct cvmx_dfa_memfadr_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_40_63               : 24;
	uint64_t fdst                         : 9;  /**< Fill-Destination
                                                            FSRC[1:0]    | FDST[8:0]
                                                            -------------+-------------------------------------
                                                             0(NCB-DTE)  | [fillstart,2'b0,WIDX(1),DMODE(1),DTE(4)]
                                                             1(NCB-CSR)  | [ncbSRC[8:0]]
                                                             3(CP2-PP)   | [2'b0,SIZE(1),INDEX(1),PP(4),FID(1)]
                                                           where:
                                                               DTE: DFA Thread Engine ID#
                                                               PP: Packet Processor ID#
                                                               FID: Fill-ID# (unique per PP)
                                                               WIDX:  16b SIMPLE Mode (index)
                                                               DMODE: (0=16b SIMPLE/1=32b SIMPLE)
                                                               SIZE: (0=LW Mode access/1=QW Mode Access)
                                                               INDEX: (0=Low LW/1=High LW)
                                                         NOTE: QW refers to a 56/64-bit LLM Load/Store (intiated
                                                         by a processor core). LW refers to a 32-bit load/store. */
	uint64_t fsrc                         : 2;  /**< Fill-Source (0=NCB-DTE/1=NCB-CSR/2=RESERVED/3=PP-CP2) */
	uint64_t pnum                         : 1;  /**< Memory Port
                                                         NOTE: For O2P, this bit will always return zero. */
	uint64_t bnum                         : 3;  /**< Memory Bank
                                                         When DFA_DDR2_ADDR[RNK_LO]=1, BNUM[2]=RANK[0].
                                                         (RANK[1] can be inferred from MADDR[24:0]) */
	uint64_t maddr                        : 25; /**< Memory Address */
#else
	uint64_t maddr                        : 25;
	uint64_t bnum                         : 3;
	uint64_t pnum                         : 1;
	uint64_t fsrc                         : 2;
	uint64_t fdst                         : 9;
	uint64_t reserved_40_63               : 24;
#endif
	} cn31xx;
	struct cvmx_dfa_memfadr_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_39_63               : 25;
	uint64_t fdst                         : 9;  /**< Fill-Destination
                                                            FSRC[1:0]    | FDST[8:0]
                                                            -------------+-------------------------------------
                                                             0(NCB-DTE)  | [fillstart,2'b0,WIDX(1),DMODE(1),DTE(4)]
                                                             1(NCB-CSR)  | [ncbSRC[8:0]]
                                                             3(CP2-PP)   | [2'b0,SIZE(1),INDEX(1),PP(4),FID(1)]
                                                           where:
                                                               DTE: DFA Thread Engine ID#
                                                               PP: Packet Processor ID#
                                                               FID: Fill-ID# (unique per PP)
                                                               WIDX:  18b SIMPLE Mode (index)
                                                               DMODE: (0=18b SIMPLE/1=36b SIMPLE)
                                                               SIZE: (0=LW Mode access/1=QW Mode Access)
                                                               INDEX: (0=Low LW/1=High LW)
                                                         NOTE: QW refers to a 64-bit LLM Load/Store (intiated
                                                         by a processor core). LW refers to a 36-bit load/store. */
	uint64_t fsrc                         : 2;  /**< Fill-Source (0=NCB-DTE/1=NCB-CSR/2=RESERVED/3=PP-CP2) */
	uint64_t pnum                         : 1;  /**< Memory Port
                                                         NOTE: the port id's are reversed
                                                            PNUM==0 => port#1
                                                            PNUM==1 => port#0 */
	uint64_t bnum                         : 3;  /**< Memory Bank */
	uint64_t maddr                        : 24; /**< Memory Address */
#else
	uint64_t maddr                        : 24;
	uint64_t bnum                         : 3;
	uint64_t pnum                         : 1;
	uint64_t fsrc                         : 2;
	uint64_t fdst                         : 9;
	uint64_t reserved_39_63               : 25;
#endif
	} cn38xx;
	struct cvmx_dfa_memfadr_cn38xx        cn38xxp2;
	struct cvmx_dfa_memfadr_cn38xx        cn58xx;
	struct cvmx_dfa_memfadr_cn38xx        cn58xxp1;
};
typedef union cvmx_dfa_memfadr cvmx_dfa_memfadr_t;

/**
 * cvmx_dfa_memfcr
 *
 * DFA_MEMFCR = FCRAM MRS Register(s) EMRS2[14:0], EMRS1[14:0], MRS[14:0]
 * *** CN58XX UNSUPPORTED ***
 *
 * Notes:
 * For FCRAM-II please consult your device's data sheet for further details:
 * MRS Definition:
 *    A[13:8]=0   RESERVED
 *    A[7]=0      TEST MODE     (N3K requires test mode 0:"disabled")
 *    A[6:4]      CAS LATENCY   (fully programmable - SW must ensure that the value programmed
 *                               into DFA_MEM_CFG0[TRL] corresponds with this value).
 *    A[3]=0      BURST TYPE    (N3K requires 0:"Sequential" Burst Type)
 *    A[2:0]      BURST LENGTH  Burst Length [1:BL2/2:BL4] (N3K only supports BL=2,4)
 *
 *                                  In BL2 mode(for highest performance), only 1/2 the phsyical
 *                                  memory is unique (ie: each bunk stores the same information).
 *                                  In BL4 mode(highest capacity), all of the physical memory
 *                                  is unique (ie: each bunk is uniquely addressable).
 * EMRS Definition:
 *    A[13:12]    REFRESH MODE  (N3K Supports only 0:"Conventional" and 1:"Short" auto-refresh modes)
 *
 *                              (SW must ensure that the value programmed into DFA_MEMCFG2[REFSHORT]
 *                              is also reflected in the Refresh Mode encoding).
 *    A[11:7]=0   RESERVED
 *    A[6:5]=2    STROBE SELECT (N3K supports only 2:"Unidirectional DS/QS" mode - the read capture
 *                              silos rely on a conditional QS strobe)
 *    A[4:3]      DIC(QS)       QS Drive Strength: fully programmable (consult your FCRAM-II data sheet)
 *                                [0: Normal Output Drive/1: Strong Output Drive/2: Weak output Drive]
 *    A[2:1]      DIC(DQ)       DQ Drive Strength: fully programmable (consult your FCRAM-II data sheet)
 *                                [0: Normal Output Drive/1: Strong Output Drive/2: Weak output Drive]
 *    A[0]        DLL           DLL Enable: Programmable [0:DLL Enable/1: DLL Disable]
 *
 * EMRS2 Definition: (for FCRAM-II+)
 *    A[13:11]=0                RESERVED
 *    A[10:8]     ODTDS         On Die Termination (DS+/-)
 *                                 [0: ODT Disable /1: 15ohm termination /(2-7): RESERVED]
 *    A[7:6]=0    MBW           Multi-Bank Write: (N3K requires use of 0:"single bank" mode only)
 *    A[5:3]      ODTin         On Die Termination (input pin)
 *                                 [0: ODT Disable /1: 15ohm termination /(2-7): RESERVED]
 *    A[2:0]      ODTDQ         On Die Termination (DQ)
 *                                 [0: ODT Disable /1: 15ohm termination /(2-7): RESERVED]
 */
union cvmx_dfa_memfcr {
	uint64_t u64;
	struct cvmx_dfa_memfcr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_47_63               : 17;
	uint64_t emrs2                        : 15; /**< Memory Address[14:0] during EMRS2(for FCRAM-II+)
                                                         *** CN58XX UNSUPPORTED *** */
	uint64_t reserved_31_31               : 1;
	uint64_t emrs                         : 15; /**< Memory Address[14:0] during EMRS
                                                         *** CN58XX UNSUPPORTED ***
                                                           A[0]=1: DLL Enabled) */
	uint64_t reserved_15_15               : 1;
	uint64_t mrs                          : 15; /**< FCRAM Memory Address[14:0] during MRS
                                                         *** CN58XX UNSUPPORTED ***
                                                           A[6:4]=4  CAS LATENCY=4(default)
                                                           A[3]=0    Burst Type(must be 0:Sequential)
                                                           A[2:0]=2  Burst Length=4(default) */
#else
	uint64_t mrs                          : 15;
	uint64_t reserved_15_15               : 1;
	uint64_t emrs                         : 15;
	uint64_t reserved_31_31               : 1;
	uint64_t emrs2                        : 15;
	uint64_t reserved_47_63               : 17;
#endif
	} s;
	struct cvmx_dfa_memfcr_s              cn38xx;
	struct cvmx_dfa_memfcr_s              cn38xxp2;
	struct cvmx_dfa_memfcr_s              cn58xx;
	struct cvmx_dfa_memfcr_s              cn58xxp1;
};
typedef union cvmx_dfa_memfcr cvmx_dfa_memfcr_t;

/**
 * cvmx_dfa_memhidat
 *
 * DFA_MEMHIDAT = DFA NCB-Direct CSR access to DFM Memory Space (High QW)
 *
 * Description:
 * DFA supports NCB-Direct CSR acccesses to DFM Memory space for debug purposes. Unfortunately, NCB-Direct accesses
 * are limited to QW-size(64bits), whereas the minimum access granularity for DFM Memory space is OW(128bits). To
 * support writes to DFM Memory space, the Hi-QW of data is sourced from the DFA_MEMHIDAT register. Recall, the
 * OW(128b) in DDR3 memory space is fixed format:
 *     OWDATA[127:118]: OWECC[9:0] 10bits of in-band OWECC SEC/DED codeword
 *                      This can be precomputed/written by SW OR
 *                      if DFM_FNTCTL[ECC_WENA]=1, DFM hardware will auto-compute the 10b OWECC and place in the
 *                      OWDATA[127:118] before being written to memory.
 *     OWDATA[117:0]:   Memory Data (contains fixed MNODE/MONODE arc formats for use by DTEs(thread engines).
 *                      Or, a user may choose to treat DFM Memory Space as 'scratch pad' in which case the
 *                      OWDATA[117:0] may contain user-specified information accessible via NCB-Direct CSR mode
 *                      accesses to DFA Memory Space.
 *  NOTE: To write to the DFA_MEMHIDAT register, a device would issue an IOBST directed at the DFA with addr[34:32]=3'b111.
 *        To read the DFA_MEMHIDAT register, a device would issue an IOBLD64 directed at the DFA with addr[34:32]=3'b111.
 *
 *  NOTE: If DFA_CONFIG[DTECLKDIS]=1 (DFA-DTE clocks disabled), reads/writes to the DFA_MEMHIDAT register do not take effect.
 *  NOTE: If FUSE[TBD]="DFA DTE disable" is blown, reads/writes to the DFA_MEMHIDAT register do not take effect.
 *
 * NOTE: PLEASE REMOVE DEFINITION FROM o68 HRM
 */
union cvmx_dfa_memhidat {
	uint64_t u64;
	struct cvmx_dfa_memhidat_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t hidat                        : 64; /**< DFA Hi-QW of Write data during NCB-Direct DFM DDR3
                                                         Memory accesses.
                                                         All DFM DDR3 memory accesses are OW(128b) references,
                                                         and since NCB-Direct Mode writes only support QW(64b),
                                                         the Hi QW of data must be sourced from a CSR register.
                                                         NOTE: This single register is 'shared' for ALL DFM
                                                         DDR3 Memory writes.
                                                         For o68: This register is UNUSED. Treat as spare bits.
                                                         NOTE: PLEASE REMOVE DEFINITION FROM o68 HRM */
#else
	uint64_t hidat                        : 64;
#endif
	} s;
	struct cvmx_dfa_memhidat_s            cn61xx;
	struct cvmx_dfa_memhidat_s            cn63xx;
	struct cvmx_dfa_memhidat_s            cn63xxp1;
	struct cvmx_dfa_memhidat_s            cn66xx;
	struct cvmx_dfa_memhidat_s            cn68xx;
	struct cvmx_dfa_memhidat_s            cn68xxp1;
};
typedef union cvmx_dfa_memhidat cvmx_dfa_memhidat_t;

/**
 * cvmx_dfa_memrld
 *
 * DFA_MEMRLD = DFA RLDRAM MRS Register Values
 *
 * Description:
 */
union cvmx_dfa_memrld {
	uint64_t u64;
	struct cvmx_dfa_memrld_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_23_63               : 41;
	uint64_t mrsdat                       : 23; /**< This field represents the data driven onto the
                                                         A[22:0] address lines during MRS(Mode Register Set)
                                                         commands (during a HW init sequence). This field
                                                         corresponds with the Mode Register Bit Map from
                                                         your RLDRAM-II device specific data sheet.
                                                            A[17:10]: RESERVED
                                                            A[9]:     ODT (on die termination)
                                                            A[8]:     Impedance Matching
                                                            A[7]:     DLL Reset
                                                            A[6]:     UNUSED
                                                            A[5]:     Address Mux  (for N3K: MUST BE ZERO)
                                                            A[4:3]:   Burst Length (for N3K: MUST BE ZERO)
                                                            A[2:0]:   Configuration (see data sheet for
                                                                      specific RLDRAM-II device).
                                                               - 000-001: CFG=1 [tRC=4/tRL=4/tWL=5]
                                                               - 010:     CFG=2 [tRC=6/tRL=6/tWL=7]
                                                               - 011:     CFG=3 [tRC=8/tRL=8/tWL=9]
                                                               - 100-111: RESERVED
                                                          NOTE: For additional density, the RLDRAM-II parts
                                                          can be 'clamshelled' (ie: two devices mounted on
                                                          different sides of the PCB board), since the BGA
                                                          pinout supports 'mirroring'.
                                                          To support a clamshell design, SW must preload
                                                          the MRSDAT[22:0] with the proper A[22:0] pin mapping
                                                          which is dependent on the 'selected' bunk/clam
                                                          (see also: DFA_MEMCFG0[BUNK_INIT] field).
                                                          NOTE: Care MUST BE TAKEN NOT to write to this register
                                                          within 64K eclk cycles of a HW INIT (see: INIT_P0/INIT_P1).
                                                          NOTE: This should only be written to a different value
                                                          during power-on SW initialization. */
#else
	uint64_t mrsdat                       : 23;
	uint64_t reserved_23_63               : 41;
#endif
	} s;
	struct cvmx_dfa_memrld_s              cn38xx;
	struct cvmx_dfa_memrld_s              cn38xxp2;
	struct cvmx_dfa_memrld_s              cn58xx;
	struct cvmx_dfa_memrld_s              cn58xxp1;
};
typedef union cvmx_dfa_memrld cvmx_dfa_memrld_t;

/**
 * cvmx_dfa_ncbctl
 *
 * DFA_NCBCTL = DFA NCB CTL Register
 *
 * Description:
 */
union cvmx_dfa_ncbctl {
	uint64_t u64;
	struct cvmx_dfa_ncbctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_11_63               : 53;
	uint64_t sbdnum                       : 5;  /**< SBD Debug Entry#
                                                         For internal use only. (DFA Scoreboard debug)
                                                         Selects which one of 32 DFA Scoreboard entries is
                                                         latched into the DFA_SBD_DBG[0-3] registers. */
	uint64_t sbdlck                       : 1;  /**< DFA Scoreboard LOCK Strobe
                                                         For internal use only. (DFA Scoreboard debug)
                                                         When written with a '1', the DFA Scoreboard Debug
                                                         registers (DFA_SBD_DBG[0-3]) are all locked down.
                                                         This allows SW to lock down the contents of the entire
                                                         SBD for a single instant in time. All subsequent reads
                                                         of the DFA scoreboard registers will return the data
                                                         from that instant in time. */
	uint64_t dcmode                       : 1;  /**< DRF-CRQ/DTE Arbiter Mode
                                                         DTE-DRF Arbiter (0=FP [LP=CRQ/HP=DTE],1=RR)
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t dtmode                       : 1;  /**< DRF-DTE Arbiter Mode
                                                         DTE-DRF Arbiter (0=FP [LP=DTE[15],...,HP=DTE[0]],1=RR)
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t pmode                        : 1;  /**< NCB-NRP Arbiter Mode
                                                         (0=Fixed Priority [LP=WQF,DFF,HP=RGF]/1=RR
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t qmode                        : 1;  /**< NCB-NRQ Arbiter Mode
                                                         (0=Fixed Priority [LP=IRF,RWF,PRF,HP=GRF]/1=RR
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t imode                        : 1;  /**< NCB-Inbound Arbiter
                                                         (0=FP [LP=NRQ,HP=NRP], 1=RR)
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
#else
	uint64_t imode                        : 1;
	uint64_t qmode                        : 1;
	uint64_t pmode                        : 1;
	uint64_t dtmode                       : 1;
	uint64_t dcmode                       : 1;
	uint64_t sbdlck                       : 1;
	uint64_t sbdnum                       : 5;
	uint64_t reserved_11_63               : 53;
#endif
	} s;
	struct cvmx_dfa_ncbctl_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_10_63               : 54;
	uint64_t sbdnum                       : 4;  /**< SBD Debug Entry#
                                                         For internal use only. (DFA Scoreboard debug)
                                                         Selects which one of 16 DFA Scoreboard entries is
                                                         latched into the DFA_SBD_DBG[0-3] registers. */
	uint64_t sbdlck                       : 1;  /**< DFA Scoreboard LOCK Strobe
                                                         For internal use only. (DFA Scoreboard debug)
                                                         When written with a '1', the DFA Scoreboard Debug
                                                         registers (DFA_SBD_DBG[0-3]) are all locked down.
                                                         This allows SW to lock down the contents of the entire
                                                         SBD for a single instant in time. All subsequent reads
                                                         of the DFA scoreboard registers will return the data
                                                         from that instant in time. */
	uint64_t dcmode                       : 1;  /**< DRF-CRQ/DTE Arbiter Mode
                                                         DTE-DRF Arbiter (0=FP [LP=CRQ/HP=DTE],1=RR)
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t dtmode                       : 1;  /**< DRF-DTE Arbiter Mode
                                                         DTE-DRF Arbiter (0=FP [LP=DTE[15],...,HP=DTE[0]],1=RR)
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t pmode                        : 1;  /**< NCB-NRP Arbiter Mode
                                                         (0=Fixed Priority [LP=WQF,DFF,HP=RGF]/1=RR
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t qmode                        : 1;  /**< NCB-NRQ Arbiter Mode
                                                         (0=Fixed Priority [LP=IRF,RWF,PRF,HP=GRF]/1=RR
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
	uint64_t imode                        : 1;  /**< NCB-Inbound Arbiter
                                                         (0=FP [LP=NRQ,HP=NRP], 1=RR)
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
#else
	uint64_t imode                        : 1;
	uint64_t qmode                        : 1;
	uint64_t pmode                        : 1;
	uint64_t dtmode                       : 1;
	uint64_t dcmode                       : 1;
	uint64_t sbdlck                       : 1;
	uint64_t sbdnum                       : 4;
	uint64_t reserved_10_63               : 54;
#endif
	} cn38xx;
	struct cvmx_dfa_ncbctl_cn38xx         cn38xxp2;
	struct cvmx_dfa_ncbctl_s              cn58xx;
	struct cvmx_dfa_ncbctl_s              cn58xxp1;
};
typedef union cvmx_dfa_ncbctl cvmx_dfa_ncbctl_t;

/**
 * cvmx_dfa_pfc0_cnt
 *
 * DFA_PFC0_CNT = DFA Performance Counter \#0
 * *FOR INTERNAL USE ONLY*
 * Description:
 */
union cvmx_dfa_pfc0_cnt {
	uint64_t u64;
	struct cvmx_dfa_pfc0_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t pfcnt0                       : 64; /**< Performance Counter \#0
                                                         When DFA_PFC_GCTL[CNT0ENA]=1, the event selected
                                                         by DFA_PFC0_CTL[EVSEL] is counted.
                                                         See also DFA_PFC_GCTL[CNT0WCLR] and DFA_PFC_GCTL
                                                         [CNT0RCLR] for special clear count cases available
                                                         for SW data collection. */
#else
	uint64_t pfcnt0                       : 64;
#endif
	} s;
	struct cvmx_dfa_pfc0_cnt_s            cn61xx;
	struct cvmx_dfa_pfc0_cnt_s            cn63xx;
	struct cvmx_dfa_pfc0_cnt_s            cn63xxp1;
	struct cvmx_dfa_pfc0_cnt_s            cn66xx;
	struct cvmx_dfa_pfc0_cnt_s            cn68xx;
	struct cvmx_dfa_pfc0_cnt_s            cn68xxp1;
};
typedef union cvmx_dfa_pfc0_cnt cvmx_dfa_pfc0_cnt_t;

/**
 * cvmx_dfa_pfc0_ctl
 *
 * DFA_PFC0_CTL = DFA Performance Counter#0 Control
 * *FOR INTERNAL USE ONLY*
 * Description:
 */
union cvmx_dfa_pfc0_ctl {
	uint64_t u64;
	struct cvmx_dfa_pfc0_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_14_63               : 50;
	uint64_t evsel                        : 6;  /**< Performance Counter#0 Event Selector
                                                         // Events [0-31] are based on PMODE(0:per cluster-DTE 1:per graph)
                                                          - 0:  \#Total Cycles
                                                          - 1:  \#LDNODE visits
                                                          - 2:  \#SDNODE visits
                                                          - 3:  \#DNODE visits (LD/SD)
                                                          - 4:  \#LCNODE visits
                                                          - 5:  \#SCNODE visits
                                                          - 6:  \#CNODE visits (LC/SC)
                                                          - 7:  \#LMNODE visits
                                                          - 8:  \#SMNODE visits
                                                          - 9:  \#MNODE visits (LM/SM)
                                                           - 10: \#MONODE visits
                                                           - 11: \#CACHE visits (DNODE,CNODE) exc: CNDRD,MPHIDX
                                                           - 12: \#CACHE visits (DNODE,CNODE)+(CNDRD,MPHIDX)
                                                           - 13: \#MEMORY visits (MNODE+MONODE)
                                                           - 14: \#CNDRDs detected (occur for SCNODE->*MNODE transitions)
                                                           - 15: \#MPHIDX detected (occur for ->LMNODE transitions)
                                                           - 16: \#RESCANs detected (occur when HASH collision is detected)
                                                           - 17: \#GWALK iterations STALLED - Packet data/Result Buffer
                                                           - 18: \#GWALK iterations NON-STALLED
                                                           - 19: \#CLOAD iterations
                                                           - 20: \#MLOAD iterations
                                                               [NOTE: If PMODE=1(per-graph) the MLOAD IWORD0.VGID will be used to discern graph#].
                                                           - 21: \#RWORD1+ writes
                                                           - 22: \#cycles Cluster is busy
                                                           - 23: \#GWALK Instructions
                                                           - 24: \#CLOAD Instructions
                                                           - 25: \#MLOAD Instructions
                                                               [NOTE: If PMODE=1(per-graph) the MLOAD IWORD0.VGID will be used to discern graph#].
                                                           - 26: \#GFREE Instructions
                                                           - 27-30: RESERVED
                                                           - 31: \# Node Transitions detected (see DFA_PFC_GCTL[SNODE,ENODE,EDNODE] registers
                                                         //=============================================================
                                                         // Events [32-63] are used ONLY FOR PMODE=0(per-cluster DTE mode):
                                                           - 32: \#cycles a specific cluster-DTE remains active(valid state)
                                                           - 33: \#cycles a specific cluster-DTE waits for Memory Response Data
                                                           - 34: \#cycles a specific cluster-DTE waits in resource stall state
                                                                  (waiting for packet data or result buffer space)
                                                           - 35: \#cycles a specific cluster-DTE waits in resource pending state
                                                           - 36-63: RESERVED
                                                         //============================================================= */
	uint64_t reserved_6_7                 : 2;
	uint64_t cldte                        : 4;  /**< Performance Counter#0 Cluster DTE Selector
                                                         When DFA_PFC_GCTL[PMODE]=0 (per-cluster DTE), this field
                                                         is used to select/monitor the cluster's DTE# for all events
                                                         associated with Performance Counter#0. */
	uint64_t clnum                        : 2;  /**< Performance Counter#0 Cluster Selector
                                                         When DFA_PFC_GCTL[PMODE]=0 (per-cluster DTE), this field
                                                         is used to select/monitor the cluster# for all events
                                                         associated with Performance Counter#0. */
#else
	uint64_t clnum                        : 2;
	uint64_t cldte                        : 4;
	uint64_t reserved_6_7                 : 2;
	uint64_t evsel                        : 6;
	uint64_t reserved_14_63               : 50;
#endif
	} s;
	struct cvmx_dfa_pfc0_ctl_s            cn61xx;
	struct cvmx_dfa_pfc0_ctl_s            cn63xx;
	struct cvmx_dfa_pfc0_ctl_s            cn63xxp1;
	struct cvmx_dfa_pfc0_ctl_s            cn66xx;
	struct cvmx_dfa_pfc0_ctl_s            cn68xx;
	struct cvmx_dfa_pfc0_ctl_s            cn68xxp1;
};
typedef union cvmx_dfa_pfc0_ctl cvmx_dfa_pfc0_ctl_t;

/**
 * cvmx_dfa_pfc1_cnt
 *
 * DFA_PFC1_CNT = DFA Performance Counter \#1
 * *FOR INTERNAL USE ONLY*
 * Description:
 */
union cvmx_dfa_pfc1_cnt {
	uint64_t u64;
	struct cvmx_dfa_pfc1_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t pfcnt1                       : 64; /**< Performance Counter \#1
                                                         When DFA_PFC_GCTL[CNT1ENA]=1, the event selected
                                                         by DFA_PFC1_CTL[EVSEL] is counted.
                                                         See also DFA_PFC_GCTL[CNT1WCLR] and DFA_PFC_GCTL
                                                         [CNT1RCLR] for special clear count cases available
                                                         for SW data collection. */
#else
	uint64_t pfcnt1                       : 64;
#endif
	} s;
	struct cvmx_dfa_pfc1_cnt_s            cn61xx;
	struct cvmx_dfa_pfc1_cnt_s            cn63xx;
	struct cvmx_dfa_pfc1_cnt_s            cn63xxp1;
	struct cvmx_dfa_pfc1_cnt_s            cn66xx;
	struct cvmx_dfa_pfc1_cnt_s            cn68xx;
	struct cvmx_dfa_pfc1_cnt_s            cn68xxp1;
};
typedef union cvmx_dfa_pfc1_cnt cvmx_dfa_pfc1_cnt_t;

/**
 * cvmx_dfa_pfc1_ctl
 *
 * DFA_PFC1_CTL = DFA Performance Counter#1 Control
 * *FOR INTERNAL USE ONLY*
 * Description:
 */
union cvmx_dfa_pfc1_ctl {
	uint64_t u64;
	struct cvmx_dfa_pfc1_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_14_63               : 50;
	uint64_t evsel                        : 6;  /**< Performance Counter#1 Event Selector
                                                         - 0:  \#Cycles
                                                         - 1:  \#LDNODE visits
                                                         - 2:  \#SDNODE visits
                                                         - 3:  \#DNODE visits (LD/SD)
                                                         - 4:  \#LCNODE visits
                                                         - 5:  \#SCNODE visits
                                                         - 6:  \#CNODE visits (LC/SC)
                                                         - 7:  \#LMNODE visits
                                                         - 8:  \#SMNODE visits
                                                         - 9:  \#MNODE visits (LM/SM)
                                                          - 10: \#MONODE visits
                                                          - 11: \#CACHE visits (DNODE,CNODE) exc: CNDRD,MPHIDX
                                                          - 12: \#CACHE visits (DNODE,CNODE)+(CNDRD,MPHIDX)
                                                          - 13: \#MEMORY visits (MNODE+MONODE)
                                                          - 14: \#CNDRDs detected (occur for SCNODE->*MNODE transitions)
                                                          - 15: \#MPHIDX detected (occur for ->LMNODE transitions)
                                                          - 16: \#RESCANs detected (occur when HASH collision is detected)
                                                          - 17: \#GWALK STALLs detected - Packet data/Result Buffer
                                                          - 18: \#GWALK DTE cycles (all DTE-GNT[3a])
                                                          - 19: \#CLOAD DTE cycles
                                                          - 20: \#MLOAD DTE cycles
                                                          - 21: \#cycles waiting for Memory Response Data
                                                          - 22: \#cycles waiting in resource stall state (waiting for packet data or result buffer space)
                                                          - 23: \#cycles waiting in resource pending state
                                                          - 24: \#RWORD1+ writes
                                                          - 25: \#DTE-VLD cycles
                                                          - 26: \#DTE Transitions detected (see DFA_PFC_GCTL[SNODE,ENODE] registers
                                                          - 27: \#GWALK Instructions
                                                          - 28: \#CLOAD Instructions
                                                          - 29: \#MLOAD Instructions
                                                          - 30: \#GFREE Instructions (== \#GFREE DTE cycles)
                                                          - 31: RESERVED
                                                          - 32: \#DTE-Busy cycles (ALL DTE-GNT strobes) */
	uint64_t reserved_6_7                 : 2;
	uint64_t cldte                        : 4;  /**< Performance Counter#1 Cluster DTE Selector
                                                         When DFA_PFC_GCTL[PMODE]=0 (per-cluster DTE), this field
                                                         is used to select/monitor the cluster's DTE# for all events
                                                         associated with Performance Counter#1. */
	uint64_t clnum                        : 2;  /**< Performance Counter#1 Cluster Selector
                                                         When DFA_PFC_GCTL[PMODE]=0 (per-cluster DTE), this field
                                                         is used to select/monitor the cluster# for all events
                                                         associated with Performance Counter#1. */
#else
	uint64_t clnum                        : 2;
	uint64_t cldte                        : 4;
	uint64_t reserved_6_7                 : 2;
	uint64_t evsel                        : 6;
	uint64_t reserved_14_63               : 50;
#endif
	} s;
	struct cvmx_dfa_pfc1_ctl_s            cn61xx;
	struct cvmx_dfa_pfc1_ctl_s            cn63xx;
	struct cvmx_dfa_pfc1_ctl_s            cn63xxp1;
	struct cvmx_dfa_pfc1_ctl_s            cn66xx;
	struct cvmx_dfa_pfc1_ctl_s            cn68xx;
	struct cvmx_dfa_pfc1_ctl_s            cn68xxp1;
};
typedef union cvmx_dfa_pfc1_ctl cvmx_dfa_pfc1_ctl_t;

/**
 * cvmx_dfa_pfc2_cnt
 *
 * DFA_PFC2_CNT = DFA Performance Counter \#2
 * *FOR INTERNAL USE ONLY*
 * Description:
 */
union cvmx_dfa_pfc2_cnt {
	uint64_t u64;
	struct cvmx_dfa_pfc2_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t pfcnt2                       : 64; /**< Performance Counter \#2
                                                         When DFA_PFC_GCTL[CNT2ENA]=1, the event selected
                                                         by DFA_PFC2_CTL[EVSEL] is counted.
                                                         See also DFA_PFC_GCTL[CNT2WCLR] and DFA_PFC_GCTL
                                                         [CNT2RCLR] for special clear count cases available
                                                         for SW data collection. */
#else
	uint64_t pfcnt2                       : 64;
#endif
	} s;
	struct cvmx_dfa_pfc2_cnt_s            cn61xx;
	struct cvmx_dfa_pfc2_cnt_s            cn63xx;
	struct cvmx_dfa_pfc2_cnt_s            cn63xxp1;
	struct cvmx_dfa_pfc2_cnt_s            cn66xx;
	struct cvmx_dfa_pfc2_cnt_s            cn68xx;
	struct cvmx_dfa_pfc2_cnt_s            cn68xxp1;
};
typedef union cvmx_dfa_pfc2_cnt cvmx_dfa_pfc2_cnt_t;

/**
 * cvmx_dfa_pfc2_ctl
 *
 * DFA_PFC2_CTL = DFA Performance Counter#2 Control
 * *FOR INTERNAL USE ONLY*
 * Description:
 */
union cvmx_dfa_pfc2_ctl {
	uint64_t u64;
	struct cvmx_dfa_pfc2_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_14_63               : 50;
	uint64_t evsel                        : 6;  /**< Performance Counter#2 Event Selector
                                                         - 0:  \#Cycles
                                                         - 1:  \#LDNODE visits
                                                         - 2:  \#SDNODE visits
                                                         - 3:  \#DNODE visits (LD/SD)
                                                         - 4:  \#LCNODE visits
                                                         - 5:  \#SCNODE visits
                                                         - 6:  \#CNODE visits (LC/SC)
                                                         - 7:  \#LMNODE visits
                                                         - 8:  \#SMNODE visits
                                                         - 9:  \#MNODE visits (LM/SM)
                                                          - 10: \#MONODE visits
                                                          - 11: \#CACHE visits (DNODE,CNODE) exc: CNDRD,MPHIDX
                                                          - 12: \#CACHE visits (DNODE,CNODE)+(CNDRD,MPHIDX)
                                                          - 13: \#MEMORY visits (MNODE+MONODE)
                                                          - 14: \#CNDRDs detected (occur for SCNODE->*MNODE transitions)
                                                          - 15: \#MPHIDX detected (occur for ->LMNODE transitions)
                                                          - 16: \#RESCANs detected (occur when HASH collision is detected)
                                                          - 17: \#GWALK STALLs detected - Packet data/Result Buffer
                                                          - 18: \#GWALK DTE cycles (all DTE-GNT[3a])
                                                          - 19: \#CLOAD DTE cycles
                                                          - 20: \#MLOAD DTE cycles
                                                          - 21: \#cycles waiting for Memory Response Data
                                                          - 22: \#cycles waiting in resource stall state (waiting for packet data or result buffer space)
                                                          - 23: \#cycles waiting in resource pending state
                                                          - 24: \#RWORD1+ writes
                                                          - 25: \#DTE-VLD cycles
                                                          - 26: \#DTE Transitions detected (see DFA_PFC_GCTL[SNODE,ENODE] registers
                                                          - 27: \#GWALK Instructions
                                                          - 28: \#CLOAD Instructions
                                                          - 29: \#MLOAD Instructions
                                                          - 30: \#GFREE Instructions (== \#GFREE DTE cycles)
                                                          - 31: RESERVED
                                                          - 32: \#DTE-Busy cycles (ALL DTE-GNT strobes) */
	uint64_t reserved_6_7                 : 2;
	uint64_t cldte                        : 4;  /**< Performance Counter#2 Cluster DTE Selector
                                                         When DFA_PFC_GCTL[PMODE]=0 (per-cluster DTE), this field
                                                         is used to select/monitor the cluster's DTE# for all events
                                                         associated with Performance Counter#2. */
	uint64_t clnum                        : 2;  /**< Performance Counter#2 Cluster Selector
                                                         When DFA_PFC_GCTL[PMODE]=0 (per-cluster DTE), this field
                                                         is used to select/monitor the cluster# for all events
                                                         associated with Performance Counter#2. */
#else
	uint64_t clnum                        : 2;
	uint64_t cldte                        : 4;
	uint64_t reserved_6_7                 : 2;
	uint64_t evsel                        : 6;
	uint64_t reserved_14_63               : 50;
#endif
	} s;
	struct cvmx_dfa_pfc2_ctl_s            cn61xx;
	struct cvmx_dfa_pfc2_ctl_s            cn63xx;
	struct cvmx_dfa_pfc2_ctl_s            cn63xxp1;
	struct cvmx_dfa_pfc2_ctl_s            cn66xx;
	struct cvmx_dfa_pfc2_ctl_s            cn68xx;
	struct cvmx_dfa_pfc2_ctl_s            cn68xxp1;
};
typedef union cvmx_dfa_pfc2_ctl cvmx_dfa_pfc2_ctl_t;

/**
 * cvmx_dfa_pfc3_cnt
 *
 * DFA_PFC3_CNT = DFA Performance Counter \#3
 * *FOR INTERNAL USE ONLY*
 * Description:
 */
union cvmx_dfa_pfc3_cnt {
	uint64_t u64;
	struct cvmx_dfa_pfc3_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t pfcnt3                       : 64; /**< Performance Counter \#3
                                                         When DFA_PFC_GCTL[CNT3ENA]=1, the event selected
                                                         by DFA_PFC3_CTL[EVSEL] is counted.
                                                         See also DFA_PFC_GCTL[CNT3WCLR] and DFA_PFC_GCTL
                                                         [CNT3RCLR] for special clear count cases available
                                                         for SW data collection. */
#else
	uint64_t pfcnt3                       : 64;
#endif
	} s;
	struct cvmx_dfa_pfc3_cnt_s            cn61xx;
	struct cvmx_dfa_pfc3_cnt_s            cn63xx;
	struct cvmx_dfa_pfc3_cnt_s            cn63xxp1;
	struct cvmx_dfa_pfc3_cnt_s            cn66xx;
	struct cvmx_dfa_pfc3_cnt_s            cn68xx;
	struct cvmx_dfa_pfc3_cnt_s            cn68xxp1;
};
typedef union cvmx_dfa_pfc3_cnt cvmx_dfa_pfc3_cnt_t;

/**
 * cvmx_dfa_pfc3_ctl
 *
 * DFA_PFC3_CTL = DFA Performance Counter#3 Control
 * *FOR INTERNAL USE ONLY*
 * Description:
 */
union cvmx_dfa_pfc3_ctl {
	uint64_t u64;
	struct cvmx_dfa_pfc3_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_14_63               : 50;
	uint64_t evsel                        : 6;  /**< Performance Counter#3 Event Selector
                                                         - 0:  \#Cycles
                                                         - 1:  \#LDNODE visits
                                                         - 2:  \#SDNODE visits
                                                         - 3:  \#DNODE visits (LD/SD)
                                                         - 4:  \#LCNODE visits
                                                         - 5:  \#SCNODE visits
                                                         - 6:  \#CNODE visits (LC/SC)
                                                         - 7:  \#LMNODE visits
                                                         - 8:  \#SMNODE visits
                                                         - 9:  \#MNODE visits (LM/SM)
                                                          - 10: \#MONODE visits
                                                          - 11: \#CACHE visits (DNODE,CNODE) exc: CNDRD,MPHIDX
                                                          - 12: \#CACHE visits (DNODE,CNODE)+(CNDRD,MPHIDX)
                                                          - 13: \#MEMORY visits (MNODE+MONODE)
                                                          - 14: \#CNDRDs detected (occur for SCNODE->*MNODE transitions)
                                                          - 15: \#MPHIDX detected (occur for ->LMNODE transitions)
                                                          - 16: \#RESCANs detected (occur when HASH collision is detected)
                                                          - 17: \#GWALK STALLs detected - Packet data/Result Buffer
                                                          - 18: \#GWALK DTE cycles (all DTE-GNT[3a])
                                                          - 19: \#CLOAD DTE cycles
                                                          - 20: \#MLOAD DTE cycles
                                                          - 21: \#cycles waiting for Memory Response Data
                                                          - 22: \#cycles waiting in resource stall state (waiting for packet data or result buffer space)
                                                          - 23: \#cycles waiting in resource pending state
                                                          - 24: \#RWORD1+ writes
                                                          - 25: \#DTE-VLD cycles
                                                          - 26: \#DTE Transitions detected (see DFA_PFC_GCTL[SNODE,ENODE] registers
                                                          - 27: \#GWALK Instructions
                                                          - 28: \#CLOAD Instructions
                                                          - 29: \#MLOAD Instructions
                                                          - 30: \#GFREE Instructions (== \#GFREE DTE cycles)
                                                          - 31: RESERVED
                                                          - 32: \#DTE-Busy cycles (ALL DTE-GNT strobes) */
	uint64_t reserved_6_7                 : 2;
	uint64_t cldte                        : 4;  /**< Performance Counter#3 Cluster DTE Selector
                                                         When DFA_PFC_GCTL[PMODE]=0 (per-cluster DTE), this field
                                                         is used to select/monitor the cluster's DTE# for all events
                                                         associated with Performance Counter#3. */
	uint64_t clnum                        : 2;  /**< Performance Counter#3 Cluster Selector
                                                         When DFA_PFC_GCTL[PMODE]=0 (per-cluster DTE), this field
                                                         is used to select/monitor the cluster# for all events
                                                         associated with Performance Counter#3. */
#else
	uint64_t clnum                        : 2;
	uint64_t cldte                        : 4;
	uint64_t reserved_6_7                 : 2;
	uint64_t evsel                        : 6;
	uint64_t reserved_14_63               : 50;
#endif
	} s;
	struct cvmx_dfa_pfc3_ctl_s            cn61xx;
	struct cvmx_dfa_pfc3_ctl_s            cn63xx;
	struct cvmx_dfa_pfc3_ctl_s            cn63xxp1;
	struct cvmx_dfa_pfc3_ctl_s            cn66xx;
	struct cvmx_dfa_pfc3_ctl_s            cn68xx;
	struct cvmx_dfa_pfc3_ctl_s            cn68xxp1;
};
typedef union cvmx_dfa_pfc3_ctl cvmx_dfa_pfc3_ctl_t;

/**
 * cvmx_dfa_pfc_gctl
 *
 * DFA_PFC_GCTL = DFA Performance Counter Global Control
 * *FOR INTERNAL USE ONLY*
 * Description:
 */
union cvmx_dfa_pfc_gctl {
	uint64_t u64;
	struct cvmx_dfa_pfc_gctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_29_63               : 35;
	uint64_t vgid                         : 8;  /**< Virtual Graph Id#
                                                         When PMODE=1(per-graph selector), this field is used
                                                         to select/monitor only those events which are
                                                         associated with this selected VGID(virtual graph ID).
                                                         This field is used globally across all four performance
                                                         counters.
                                                         IMPNOTE: I implemented a global VGID across all 4 performance
                                                         counters to save wires/area. */
	uint64_t pmode                        : 1;  /**< Select Mode
                                                         - 0: Events are selected on a per-cluster DTE# (CLNUM/CLDTE)
                                                          DFA_PFCx_CTL[CLNUM,CLDTE] specifies the cluster-DTE for
                                                          each 1(of 4) performance counters.
                                                         - 1: Events are selected on a per-graph basis (VGID=virtual Graph ID).
                                                          NOTE: Only EVSEL=[0...31] can be used in conjunction with PMODE=1.
                                                          DFA_PFC_GCTL[VGID] specifies the Virtual graph ID used across
                                                          all four performance counters. */
	uint64_t ednode                       : 2;  /**< Ending DNODE Selector
                                                         When ENODE=0/1(*DNODE), this field is used to further
                                                         specify the Ending DNODE transition sub-type:
                                                           - 0: ALL DNODE sub-types
                                                           - 1: ->D2e (explicit DNODE transition node-arc alone transitions to DNODE)
                                                           - 2: ->D2i (implicit DNODE transition:arc-present triggers transition)
                                                           - 3: ->D1r (rescan DNODE transition) */
	uint64_t enode                        : 3;  /**< Ending Node Selector
                                                         When DFA_PFCx_CTL[EVSEL]=Node Transition(31), the ENODE
                                                         field is used to select Ending Node, and the SNODE
                                                         field is used to select the Starting Node.
                                                          - 0: LDNODE
                                                          - 1: SDNODE
                                                          - 2: LCNODE
                                                          - 3: SCNODE
                                                          - 4: LMNODE
                                                          - 5: SMNODE
                                                          - 6: MONODE
                                                          - 7: RESERVED */
	uint64_t snode                        : 3;  /**< Starting Node Selector
                                                         When DFA_PFCx_CTL[EVSEL]=Node Transition(31), the SNODE
                                                         field is used to select Starting Node, and the ENODE
                                                         field is used to select the Ending Node.
                                                          - 0: LDNODE
                                                          - 1: SDNODE
                                                          - 2: LCNODE
                                                          - 3: SCNODE
                                                          - 4: LMNODE
                                                          - 5: SMNODE
                                                          - 6: MONODE
                                                          - 7: RESERVED */
	uint64_t cnt3rclr                     : 1;  /**< Performance Counter \#3 Read Clear
                                                         If this bit is set, CSR reads to the DFA_PFC3_CNT
                                                         will clear the count value. This allows SW to maintain
                                                         'cumulative' counters to avoid HW wraparound. */
	uint64_t cnt2rclr                     : 1;  /**< Performance Counter \#2 Read Clear
                                                         If this bit is set, CSR reads to the DFA_PFC2_CNT
                                                         will clear the count value. This allows SW to maintain
                                                         'cumulative' counters to avoid HW wraparound. */
	uint64_t cnt1rclr                     : 1;  /**< Performance Counter \#1 Read Clear
                                                         If this bit is set, CSR reads to the DFA_PFC1_CNT
                                                         will clear the count value. This allows SW to maintain
                                                         'cumulative' counters to avoid HW wraparound. */
	uint64_t cnt0rclr                     : 1;  /**< Performance Counter \#0 Read Clear
                                                         If this bit is set, CSR reads to the DFA_PFC0_CNT
                                                         will clear the count value. This allows SW to maintain
                                                         'cumulative' counters to avoid HW wraparound. */
	uint64_t cnt3wclr                     : 1;  /**< Performance Counter \#3 Write Clear
                                                         If this bit is set, CSR writes to the DFA_PFC3_CNT
                                                         will clear the count value.
                                                         If this bit is clear, CSR writes to the DFA_PFC3_CNT
                                                         will continue the count from the written value. */
	uint64_t cnt2wclr                     : 1;  /**< Performance Counter \#2 Write Clear
                                                         If this bit is set, CSR writes to the DFA_PFC2_CNT
                                                         will clear the count value.
                                                         If this bit is clear, CSR writes to the DFA_PFC2_CNT
                                                         will continue the count from the written value. */
	uint64_t cnt1wclr                     : 1;  /**< Performance Counter \#1 Write Clear
                                                         If this bit is set, CSR writes to the DFA_PFC1_CNT
                                                         will clear the count value.
                                                         If this bit is clear, CSR writes to the DFA_PFC1_CNT
                                                         will continue the count from the written value. */
	uint64_t cnt0wclr                     : 1;  /**< Performance Counter \#0 Write Clear
                                                         If this bit is set, CSR writes to the DFA_PFC0_CNT
                                                         will clear the count value.
                                                         If this bit is clear, CSR writes to the DFA_PFC0_CNT
                                                         will continue the count from the written value. */
	uint64_t cnt3ena                      : 1;  /**< Performance Counter 3 Enable
                                                         When this bit is set, the performance counter \#3
                                                         is enabled. */
	uint64_t cnt2ena                      : 1;  /**< Performance Counter 2 Enable
                                                         When this bit is set, the performance counter \#2
                                                         is enabled. */
	uint64_t cnt1ena                      : 1;  /**< Performance Counter 1 Enable
                                                         When this bit is set, the performance counter \#1
                                                         is enabled. */
	uint64_t cnt0ena                      : 1;  /**< Performance Counter 0 Enable
                                                         When this bit is set, the performance counter \#0
                                                         is enabled. */
#else
	uint64_t cnt0ena                      : 1;
	uint64_t cnt1ena                      : 1;
	uint64_t cnt2ena                      : 1;
	uint64_t cnt3ena                      : 1;
	uint64_t cnt0wclr                     : 1;
	uint64_t cnt1wclr                     : 1;
	uint64_t cnt2wclr                     : 1;
	uint64_t cnt3wclr                     : 1;
	uint64_t cnt0rclr                     : 1;
	uint64_t cnt1rclr                     : 1;
	uint64_t cnt2rclr                     : 1;
	uint64_t cnt3rclr                     : 1;
	uint64_t snode                        : 3;
	uint64_t enode                        : 3;
	uint64_t ednode                       : 2;
	uint64_t pmode                        : 1;
	uint64_t vgid                         : 8;
	uint64_t reserved_29_63               : 35;
#endif
	} s;
	struct cvmx_dfa_pfc_gctl_s            cn61xx;
	struct cvmx_dfa_pfc_gctl_s            cn63xx;
	struct cvmx_dfa_pfc_gctl_s            cn63xxp1;
	struct cvmx_dfa_pfc_gctl_s            cn66xx;
	struct cvmx_dfa_pfc_gctl_s            cn68xx;
	struct cvmx_dfa_pfc_gctl_s            cn68xxp1;
};
typedef union cvmx_dfa_pfc_gctl cvmx_dfa_pfc_gctl_t;

/**
 * cvmx_dfa_rodt_comp_ctl
 *
 * DFA_RODT_COMP_CTL = DFA RLD Compensation control (For read "on die termination")
 *
 */
union cvmx_dfa_rodt_comp_ctl {
	uint64_t u64;
	struct cvmx_dfa_rodt_comp_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_17_63               : 47;
	uint64_t enable                       : 1;  /**< Read On Die Termination Enable
                                                         (0=disable, 1=enable) */
	uint64_t reserved_12_15               : 4;
	uint64_t nctl                         : 4;  /**< Compensation control bits */
	uint64_t reserved_5_7                 : 3;
	uint64_t pctl                         : 5;  /**< Compensation control bits */
#else
	uint64_t pctl                         : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t nctl                         : 4;
	uint64_t reserved_12_15               : 4;
	uint64_t enable                       : 1;
	uint64_t reserved_17_63               : 47;
#endif
	} s;
	struct cvmx_dfa_rodt_comp_ctl_s       cn58xx;
	struct cvmx_dfa_rodt_comp_ctl_s       cn58xxp1;
};
typedef union cvmx_dfa_rodt_comp_ctl cvmx_dfa_rodt_comp_ctl_t;

/**
 * cvmx_dfa_sbd_dbg0
 *
 * DFA_SBD_DBG0 = DFA Scoreboard Debug \#0 Register
 *
 * Description: When the DFA_NCBCTL[SBDLCK] bit is written '1', the contents of this register are locked down.
 * Otherwise, the contents of this register are the 'active' contents of the DFA Scoreboard at the time of the
 * CSR read.
 * VERIFICATION NOTE: Read data is unsafe. X's(undefined data) can propagate (in the behavioral model)
 * on the reads unless the DTE Engine specified by DFA_NCBCTL[SBDNUM] has previously been assigned an
 * instruction.
 */
union cvmx_dfa_sbd_dbg0 {
	uint64_t u64;
	struct cvmx_dfa_sbd_dbg0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t sbd0                         : 64; /**< DFA ScoreBoard \#0 Data
                                                         For internal use only! (DFA Scoreboard Debug)
                                                         [63:40] rptr[26:3]: Result Base Pointer
                                                         [39:24] rwcnt[15:0] Cumulative Result Write Counter
                                                         [23]    lastgrdrsp: Last Gather-Rd Response
                                                         [22]    wtgrdrsp: Waiting Gather-Rd Response
                                                         [21]    wtgrdreq: Waiting for Gather-Rd Issue
                                                         [20]    glvld: GLPTR/GLCNT Valid
                                                         [19]    cmpmark: Completion Marked Node Detected
                                                         [18:17] cmpcode[1:0]: Completion Code
                                                                       [0=PDGONE/1=PERR/2=RFULL/3=TERM]
                                                         [16]    cmpdet: Completion Detected
                                                         [15]    wthdrwrcmtrsp: Waiting for HDR RWrCmtRsp
                                                         [14]    wtlastwrcmtrsp: Waiting for LAST RESULT
                                                                       RWrCmtRsp
                                                         [13]    hdrwrreq: Waiting for HDR RWrReq
                                                         [12]    wtrwrreq: Waiting for RWrReq
                                                         [11]    wtwqwrreq: Waiting for WQWrReq issue
                                                         [10]    lastprdrspeot: Last Packet-Rd Response
                                                         [9]     lastprdrsp: Last Packet-Rd Response
                                                         [8]     wtprdrsp:  Waiting for PRdRsp EOT
                                                         [7]     wtprdreq: Waiting for PRdReq Issue
                                                         [6]     lastpdvld: PDPTR/PDLEN Valid
                                                         [5]     pdvld: Packet Data Valid
                                                         [4]     wqvld: WQVLD
                                                         [3]     wqdone: WorkQueue Done condition
                                                                       a) WQWrReq issued(for WQPTR<>0) OR
                                                                       b) HDR RWrCmtRsp completed)
                                                         [2]     rwstf: Resultant write STF/P Mode
                                                         [1]     pdldt: Packet-Data LDT mode
                                                         [0]     gmode: Gather-Mode */
#else
	uint64_t sbd0                         : 64;
#endif
	} s;
	struct cvmx_dfa_sbd_dbg0_s            cn31xx;
	struct cvmx_dfa_sbd_dbg0_s            cn38xx;
	struct cvmx_dfa_sbd_dbg0_s            cn38xxp2;
	struct cvmx_dfa_sbd_dbg0_s            cn58xx;
	struct cvmx_dfa_sbd_dbg0_s            cn58xxp1;
};
typedef union cvmx_dfa_sbd_dbg0 cvmx_dfa_sbd_dbg0_t;

/**
 * cvmx_dfa_sbd_dbg1
 *
 * DFA_SBD_DBG1 = DFA Scoreboard Debug \#1 Register
 *
 * Description: When the DFA_NCBCTL[SBDLCK] bit is written '1', the contents of this register are locked down.
 * Otherwise, the contents of this register are the 'active' contents of the DFA Scoreboard at the time of the
 * CSR read.
 * VERIFICATION NOTE: Read data is unsafe. X's(undefined data) can propagate (in the behavioral model)
 * on the reads unless the DTE Engine specified by DFA_NCBCTL[SBDNUM] has previously been assigned an
 * instruction.
 */
union cvmx_dfa_sbd_dbg1 {
	uint64_t u64;
	struct cvmx_dfa_sbd_dbg1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t sbd1                         : 64; /**< DFA ScoreBoard \#1 Data
                                                         For internal use only! (DFA Scoreboard Debug)
                                                         [63:61] wqptr[35:33]: Work Queue Pointer
                                                         [60:52] rptr[35:27]: Result Base Pointer
                                                         [51:16] pdptr[35:0]: Packet Data Pointer
                                                         [15:0]  pdcnt[15:0]: Packet Data Counter */
#else
	uint64_t sbd1                         : 64;
#endif
	} s;
	struct cvmx_dfa_sbd_dbg1_s            cn31xx;
	struct cvmx_dfa_sbd_dbg1_s            cn38xx;
	struct cvmx_dfa_sbd_dbg1_s            cn38xxp2;
	struct cvmx_dfa_sbd_dbg1_s            cn58xx;
	struct cvmx_dfa_sbd_dbg1_s            cn58xxp1;
};
typedef union cvmx_dfa_sbd_dbg1 cvmx_dfa_sbd_dbg1_t;

/**
 * cvmx_dfa_sbd_dbg2
 *
 * DFA_SBD_DBG2 = DFA Scoreboard Debug \#2 Register
 *
 * Description: When the DFA_NCBCTL[SBDLCK] bit is written '1', the contents of this register are locked down.
 * Otherwise, the contents of this register are the 'active' contents of the DFA Scoreboard at the time of the
 * CSR read.
 * VERIFICATION NOTE: Read data is unsafe. X's(undefined data) can propagate (in the behavioral model)
 * on the reads unless the DTE Engine specified by DFA_NCBCTL[SBDNUM] has previously been assigned an
 * instruction.
 */
union cvmx_dfa_sbd_dbg2 {
	uint64_t u64;
	struct cvmx_dfa_sbd_dbg2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t sbd2                         : 64; /**< DFA ScoreBoard \#2 Data
                                                         [63:49] wqptr[17:3]: Work Queue Pointer
                                                         [48:16] rwptr[35:3]: Result Write Pointer
                                                         [15:0]  prwcnt[15:0]: Pending Result Write Counter */
#else
	uint64_t sbd2                         : 64;
#endif
	} s;
	struct cvmx_dfa_sbd_dbg2_s            cn31xx;
	struct cvmx_dfa_sbd_dbg2_s            cn38xx;
	struct cvmx_dfa_sbd_dbg2_s            cn38xxp2;
	struct cvmx_dfa_sbd_dbg2_s            cn58xx;
	struct cvmx_dfa_sbd_dbg2_s            cn58xxp1;
};
typedef union cvmx_dfa_sbd_dbg2 cvmx_dfa_sbd_dbg2_t;

/**
 * cvmx_dfa_sbd_dbg3
 *
 * DFA_SBD_DBG3 = DFA Scoreboard Debug \#3 Register
 *
 * Description: When the DFA_NCBCTL[SBDLCK] bit is written '1', the contents of this register are locked down.
 * Otherwise, the contents of this register are the 'active' contents of the DFA Scoreboard at the time of the
 * CSR read.
 * VERIFICATION NOTE: Read data is unsafe. X's(undefined data) can propagate (in the behavioral model)
 * on the reads unless the DTE Engine specified by DFA_NCBCTL[SBDNUM] has previously been assigned an
 * instruction.
 */
union cvmx_dfa_sbd_dbg3 {
	uint64_t u64;
	struct cvmx_dfa_sbd_dbg3_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t sbd3                         : 64; /**< DFA ScoreBoard \#3 Data
                                                         [63:49] wqptr[32:18]: Work Queue Pointer
                                                         [48:16] glptr[35:3]: Gather List Pointer
                                                         [15:0]  glcnt[15:0]: Gather List Counter */
#else
	uint64_t sbd3                         : 64;
#endif
	} s;
	struct cvmx_dfa_sbd_dbg3_s            cn31xx;
	struct cvmx_dfa_sbd_dbg3_s            cn38xx;
	struct cvmx_dfa_sbd_dbg3_s            cn38xxp2;
	struct cvmx_dfa_sbd_dbg3_s            cn58xx;
	struct cvmx_dfa_sbd_dbg3_s            cn58xxp1;
};
typedef union cvmx_dfa_sbd_dbg3 cvmx_dfa_sbd_dbg3_t;

#endif
